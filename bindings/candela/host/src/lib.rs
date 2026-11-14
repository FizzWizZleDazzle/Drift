//! Runs a Candela game inside Drift.
//!
//! The host compiles the game's entry script with the `drift` module, reads
//! the `@drift` attributes, and then, once a frame, calls every system with
//! the data its parameters ask for: query rows, lookups, resources, messages,
//! collisions and engine resources. Components and resources of the script
//! live here, as candela values; engine components live in the engine and
//! cross through the C++ bridge as JSON whose shape matches the `drift`
//! module's structs. What a system changed comes back with `call_in_out` and
//! is kept.

use candela::{
    AttributeTable, AttributeTarget, AttributeValue, DataType, Diagnostic, Engine, HostError, HostRegistry, HostType,
    IntoHostFn, Program, RuntimeProgram, Value,
};
use serde_json::{json, Map as JsonMap, Value as Json};
use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::ffi::{c_char, CStr, CString};

mod check;
mod reload;
mod wire;

// ---- The C++ side ----

extern "C" {
    fn drift_bridge_log(level: i32, message: *const c_char);
    fn drift_bridge_new() -> u64;
    fn drift_bridge_despawn(e: u64);
    fn drift_bridge_alive(e: u64) -> i32;
    fn drift_bridge_is_engine(component: *const c_char) -> i32;
    fn drift_bridge_has(e: u64, component: *const c_char) -> i32;
    // Values cross as MessagePack (wire.rs). An answer stays valid until
    // the next call.
    fn drift_bridge_get(e: u64, component: *const c_char, len: *mut usize) -> *const u8;
    fn drift_bridge_set(e: u64, component: *const c_char, data: *const u8, len: usize) -> i32;
    fn drift_bridge_remove(e: u64, component: *const c_char);
    fn drift_bridge_list(component: *const c_char, len: *mut usize) -> *const u8;
    fn drift_bridge_call(function: *const c_char, args: *const u8, args_len: usize, len: *mut usize) -> *const u8;
    fn drift_bridge_requires(component: *const c_char) -> *const c_char;
}

fn cstr(s: &str) -> CString {
    CString::new(s.replace('\0', "")).unwrap_or_default()
}

fn from_c(p: *const c_char) -> Option<String> {
    if p.is_null() {
        return None;
    }
    Some(unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned())
}

fn log(level: i32, message: &str) {
    let m = cstr(message);
    unsafe { drift_bridge_log(level, m.as_ptr()) };
}

/// Whether the engine has a component of this name. The engine's set of
/// components never changes, so each answer is kept.
fn is_engine(component: &str) -> bool {
    if let Some(known) = ENGINE_COMPONENTS.with(|k| k.borrow().get(component).copied()) {
        return known;
    }
    let c = cstr(component);
    let answer = unsafe { drift_bridge_is_engine(c.as_ptr()) != 0 };
    ENGINE_COMPONENTS.with(|k| k.borrow_mut().insert(component.to_owned(), answer));
    answer
}

/// The value an engine answer holds: `len` bytes of MessagePack at `p`.
fn answer(p: *const u8, len: usize) -> Option<Json> {
    if p.is_null() {
        return None;
    }
    wire::read_json(unsafe { std::slice::from_raw_parts(p, len) })
}

/// Engine component `component` of entity `e` as a candela value of the
/// struct `id`, read straight from the engine's bytes.
fn engine_get(e: u64, component: &str, types: &Types, id: u16) -> Option<Value> {
    let c = cstr(component);
    let mut len = 0;
    let p = unsafe { drift_bridge_get(e, c.as_ptr(), &mut len) };
    if p.is_null() {
        return None;
    }
    let bytes = unsafe { std::slice::from_raw_parts(p, len) };
    Some(types.from_wire(wire::Item::new(bytes), &DataType::Struct(id)))
}

fn engine_set(e: u64, component: &str, value: &Value) {
    let mut bytes = Vec::new();
    wire::write_value(&mut bytes, value);
    engine_set_bytes(e, component, &bytes);
}

fn engine_set_bytes(e: u64, component: &str, bytes: &[u8]) {
    let c = cstr(component);
    unsafe { drift_bridge_set(e, c.as_ptr(), bytes.as_ptr(), bytes.len()) };
}

fn engine_set_json(e: u64, component: &str, value: &Json) {
    let c = cstr(component);
    let mut bytes = Vec::new();
    wire::write_json(&mut bytes, value);
    unsafe { drift_bridge_set(e, c.as_ptr(), bytes.as_ptr(), bytes.len()) };
}

fn engine_list(component: &str) -> Vec<u64> {
    let c = cstr(component);
    let mut len = 0;
    let p = unsafe { drift_bridge_list(c.as_ptr(), &mut len) };
    if p.is_null() {
        return Vec::new();
    }
    let bytes = unsafe { std::slice::from_raw_parts(p, len) };
    wire::Item::new(bytes)
        .elements()
        .filter_map(|x| match x.kind() {
            wire::Kind::Int(i) => u64::try_from(i).ok(),
            _ => None,
        })
        .collect()
}

/// Calls an engine function with its arguments and reads its answer.
fn call(function: &str, args: Json) -> Json {
    let mut bytes = Vec::new();
    wire::write_json(&mut bytes, &args);
    call_bytes(function, &bytes)
}

fn call_bytes(function: &str, args: &[u8]) -> Json {
    let f = cstr(function);
    let mut len = 0;
    let p = unsafe { drift_bridge_call(f.as_ptr(), args.as_ptr(), args.len(), &mut len) };
    answer(p, len).unwrap_or(Json::Null)
}

// ---- Values and JSON ----

/// A candela value as JSON: structs as objects by field name, enums as their
/// variant name (or `{variant, payload}` when they carry data).
fn to_json(v: &Value) -> Json {
    match v {
        Value::Null => Json::Null,
        Value::Int(i) => json!(i),
        Value::Float(f) => json!(f),
        Value::Bool(b) => json!(b),
        Value::String(s) => json!(s),
        Value::Array(items) => Json::Array(items.iter().map(to_json).collect()),
        Value::Map(m) => Json::Object(m.iter().map(|(k, v)| (k.clone(), to_json(v))).collect()),
        Value::Struct { fields, .. } => {
            Json::Object(fields.iter().map(|(k, v)| (k.clone(), to_json(v))).collect())
        }
        Value::Enum { variant, payload } => {
            if payload.is_empty() {
                json!(variant)
            } else {
                json!({ "variant": variant, "payload": payload.iter().map(to_json).collect::<Vec<_>>() })
            }
        }
    }
}

struct Types {
    table: AttributeTable,
    /// Struct ids by name.
    ids: HashMap<String, u16>,
}

impl Types {
    fn new(table: AttributeTable) -> Self {
        let mut ids = HashMap::new();
        for (i, s) in table.structs.iter().enumerate() {
            ids.entry(s.name.clone()).or_insert(i as u16);
        }
        Types { table, ids }
    }

    fn struct_name(&self, id: u16) -> &str {
        &self.table.structs[id as usize].name
    }

    fn struct_id(&self, name: &str) -> Option<u16> {
        self.ids.get(name).copied()
    }

    /// A value of type `ty` read from JSON; what JSON leaves out takes the
    /// zero value of its type.
    fn from_json(&self, j: &Json, ty: &DataType) -> Value {
        self.from_json_with(j, ty, None)
    }

    /// from_json, with entity references read through `entity`: scene files
    /// hold an Entity as the file's id of the entity (or null for none).
    fn from_json_with(&self, j: &Json, ty: &DataType, entity: Option<&dyn Fn(&Json) -> i64>) -> Value {
        if let (Some(f), DataType::Struct(id)) = (entity, ty) {
            let info = &self.table.structs[*id as usize];
            if bare(&info.name) == "Entity" {
                return Value::Struct { name: info.name.clone(), fields: vec![("id".into(), Value::Int(f(j)))] };
            }
        }
        // Text stands in for a number or a bool (a CSV cell is always text),
        // and a number for text.
        let text = j.as_str().map(str::trim);
        match ty {
            DataType::Int => Value::Int(
                j.as_i64()
                    .or_else(|| j.as_f64().map(|f| f as i64))
                    .or_else(|| text.and_then(|t| t.parse::<i64>().ok().or_else(|| t.parse::<f64>().ok().map(|f| f as i64))))
                    .unwrap_or(0),
            ),
            DataType::Float => Value::Float(j.as_f64().or_else(|| text.and_then(|t| t.parse().ok())).unwrap_or(0.0)),
            DataType::Bool => Value::Bool(j.as_bool().or_else(|| text.map(|t| t == "true" || t == "1")).unwrap_or(false)),
            DataType::String => Value::String(match j {
                Json::String(s) => s.clone(),
                Json::Number(n) => n.to_string(),
                Json::Bool(b) => b.to_string(),
                _ => String::new(),
            }),
            DataType::Array(elem) => {
                let elem = elem.as_deref().unwrap_or(&DataType::Unknown);
                Value::Array(j.as_array().map(|a| a.iter().map(|e| self.from_json_with(e, elem, entity)).collect()).unwrap_or_default())
            }
            DataType::Map(kv) => {
                let elem = kv.1.as_ref().unwrap_or(&DataType::Unknown);
                Value::Map(
                    j.as_object()
                        .map(|o| o.iter().map(|(k, v)| (k.clone(), self.from_json_with(v, elem, entity))).collect())
                        .unwrap_or_default(),
                )
            }
            DataType::Struct(id) => {
                let info = &self.table.structs[*id as usize];
                let empty = JsonMap::new();
                let obj = j.as_object().unwrap_or(&empty);
                Value::Struct {
                    name: info.name.clone(),
                    fields: info
                        .fields
                        .iter()
                        .map(|(f, t)| (f.clone(), self.from_json_with(obj.get(f).unwrap_or(&Json::Null), t, entity)))
                        .collect(),
                }
            }
            DataType::Enum(id) => {
                let info = &self.table.enums[*id as usize];
                let name = j
                    .as_str()
                    .or_else(|| j.get("variant").and_then(Json::as_str))
                    .filter(|n| info.variants.iter().any(|v| v == n))
                    .map(str::to_owned)
                    .unwrap_or_else(|| info.variants.first().cloned().unwrap_or_default());
                Value::Enum { variant: name, payload: Vec::new() }
            }
            _ => match j {
                Json::Bool(b) => Value::Bool(*b),
                Json::Number(n) => n.as_i64().map_or_else(|| Value::Float(n.as_f64().unwrap_or(0.0)), Value::Int),
                Json::String(s) => Value::String(s.clone()),
                Json::Array(a) => Value::Array(a.iter().map(|e| self.from_json(e, &DataType::Unknown)).collect()),
                Json::Object(o) => {
                    Value::Map(o.iter().map(|(k, v)| (k.clone(), self.from_json(v, &DataType::Unknown))).collect())
                }
                Json::Null => Value::Null,
            },
        }
    }

    /// A value of type `ty` read straight from the engine's MessagePack, as
    /// `from_json` reads the same value as JSON: what the bytes leave out
    /// takes the zero value of its type.
    fn from_wire(&self, item: wire::Item<'_>, ty: &DataType) -> Value {
        use wire::Kind;
        let kind = item.kind();
        match ty {
            DataType::Int => Value::Int(match kind {
                Kind::Int(i) => i,
                Kind::Float(f) => f as i64,
                Kind::Str(t) => t.trim().parse::<i64>().ok().or_else(|| t.trim().parse::<f64>().ok().map(|f| f as i64)).unwrap_or(0),
                _ => 0,
            }),
            DataType::Float => Value::Float(match kind {
                Kind::Int(i) => i as f64,
                Kind::Float(f) => f,
                Kind::Str(t) => t.trim().parse().unwrap_or(0.0),
                _ => 0.0,
            }),
            DataType::Bool => Value::Bool(match kind {
                Kind::Bool(b) => b,
                Kind::Str(t) => t.trim() == "true" || t.trim() == "1",
                _ => false,
            }),
            DataType::String => Value::String(match kind {
                Kind::Str(s) => s.to_owned(),
                Kind::Int(i) => i.to_string(),
                Kind::Float(f) => Json::from(f).to_string(),
                Kind::Bool(b) => b.to_string(),
                _ => String::new(),
            }),
            DataType::Array(elem) => {
                let elem = elem.as_deref().unwrap_or(&DataType::Unknown);
                Value::Array(item.elements().map(|e| self.from_wire(e, elem)).collect())
            }
            DataType::Map(kv) => {
                let elem = kv.1.as_ref().unwrap_or(&DataType::Unknown);
                Value::Map(item.entries().map(|(k, v)| (k.to_owned(), self.from_wire(v, elem))).collect())
            }
            DataType::Struct(id) => {
                let info = &self.table.structs[*id as usize];
                // The engine writes a struct's fields in its own order: note
                // where each one is, then take them in declaration order.
                let mut found: [(&str, Option<wire::Item<'_>>); 48] = [("", None); 48];
                let mut n = 0;
                let mut more: Vec<(&str, wire::Item<'_>)> = Vec::new();
                for (k, v) in item.entries() {
                    if n < found.len() {
                        found[n] = (k, Some(v));
                        n += 1;
                    } else {
                        more.push((k, v));
                    }
                }
                let lookup = |name: &str| {
                    found[..n]
                        .iter()
                        .find(|(k, _)| *k == name)
                        .and_then(|(_, v)| *v)
                        .or_else(|| more.iter().find(|(k, _)| *k == name).map(|(_, v)| *v))
                };
                Value::Struct {
                    name: info.name.clone(),
                    fields: info
                        .fields
                        .iter()
                        .map(|(f, t)| {
                            let v = match lookup(f) {
                                Some(v) => self.from_wire(v, t),
                                None => self.from_json(&Json::Null, t),
                            };
                            (f.clone(), v)
                        })
                        .collect(),
                }
            }
            DataType::Enum(id) => {
                let info = &self.table.enums[*id as usize];
                let named = match kind {
                    Kind::Str(s) => Some(s),
                    Kind::Map(_) => item.entries().find(|(k, _)| *k == "variant").and_then(|(_, v)| match v.kind() {
                        Kind::Str(s) => Some(s),
                        _ => None,
                    }),
                    _ => None,
                };
                let name = named
                    .filter(|n| info.variants.iter().any(|v| v == n))
                    .map(str::to_owned)
                    .unwrap_or_else(|| info.variants.first().cloned().unwrap_or_default());
                Value::Enum { variant: name, payload: Vec::new() }
            }
            _ => match kind {
                Kind::Bool(b) => Value::Bool(b),
                Kind::Int(i) => Value::Int(i),
                Kind::Float(f) => Value::Float(f),
                Kind::Str(s) => Value::String(s.to_owned()),
                Kind::Array(_) => Value::Array(item.elements().map(|e| self.from_wire(e, &DataType::Unknown)).collect()),
                Kind::Map(_) => {
                    Value::Map(item.entries().map(|(k, v)| (k.to_owned(), self.from_wire(v, &DataType::Unknown))).collect())
                }
                Kind::Null | Kind::Other => Value::Null,
            },
        }
    }

    /// The type a `type()` answer spells: `int`, `Item`, `Item[]`,
    /// `{string: Item}`, `Pair<int, Item>`. Structs and enums match by name,
    /// then by the name without its module.
    fn parse_type(&self, name: &str) -> Option<DataType> {
        let name = name.trim();
        if let Some(inner) = name.strip_suffix("[]") {
            return Some(DataType::Array(Some(Box::new(self.parse_type(inner)?))));
        }
        if let Some(inner) = name.strip_prefix('{').and_then(|n| n.strip_suffix('}')) {
            let (k, v) = split_top(inner, ':')?;
            return Some(DataType::Map(Box::new((Some(self.parse_type(k)?), Some(self.parse_type(v)?)))));
        }
        match name {
            "int" => return Some(DataType::Int),
            "float" => return Some(DataType::Float),
            "bool" => return Some(DataType::Bool),
            "string" => return Some(DataType::String),
            "any" => return Some(DataType::Unknown),
            _ => {}
        }
        let t = &self.table;
        let find = |pick: &dyn Fn(&str) -> bool| -> Option<DataType> {
            if let Some(i) = t.structs.iter().position(|s| pick(&s.name)) {
                return Some(DataType::Struct(i as u16));
            }
            t.enums.iter().position(|e| pick(&e.name)).map(|i| DataType::Enum(i as u16))
        };
        find(&|n| n == name).or_else(|| find(&|n| bare_type(n) == bare_type(name)))
    }
}

/// `a: b` split at its first top-level `sep`.
fn split_top(s: &str, sep: char) -> Option<(&str, &str)> {
    let mut depth = 0;
    for (i, ch) in s.char_indices() {
        match ch {
            '<' | '{' | '(' => depth += 1,
            '>' | '}' | ')' => depth -= 1,
            c if c == sep && depth == 0 => return Some((&s[..i], &s[i + 1..])),
            _ => {}
        }
    }
    None
}

/// A type name without the module of its base: `drift::Pair<int>` -> `Pair<int>`.
fn bare_type(name: &str) -> &str {
    let open = name.find('<').unwrap_or(name.len());
    match name[..open].rfind("::") {
        Some(i) => &name[i + 2..],
        None => name,
    }
}

impl Types {
    /// `base` with the fields `j` names replaced, for settings sections.
    /// What is wrong with the drift.toml section `section`, `j`, as values of
    /// struct `id`: keys the struct has no field for, and values that do
    /// not fit their field, which the merge leaves at their defaults. Each
    /// with its `section.key`.
    fn section_problems(&self, j: &Json, id: u16, section: &str) -> Vec<(String, String)> {
        let info = &self.table.structs[id as usize];
        let Some(obj) = j.as_object() else { return Vec::new() };
        let names: Vec<&str> = info.fields.iter().map(|(n, _)| n.as_str()).collect();
        let mut out = Vec::new();
        for (key, value) in obj {
            let path = format!("{section}.{key}");
            match info.fields.iter().find(|(n, _)| n == key) {
                None => out.push((
                    path,
                    format!("drift_unknown_setting: [{section}] has no key '{key}' ({} has {})", bare(&info.name), names.join(", ")),
                )),
                Some((_, t)) => {
                    if let Some(need) = self.json_needs(value, t) {
                        out.push((
                            path.clone(),
                            format!("drift_bad_setting: {path} needs {need}; it keeps its default"),
                        ));
                    }
                }
            }
        }
        out
    }

    /// What `j` would have to be to fit `ty`, when it does not.
    fn json_needs(&self, j: &Json, ty: &DataType) -> Option<String> {
        let fits = |ok: bool, need: &str| (!ok).then(|| need.to_owned());
        match ty {
            DataType::Int => fits(j.is_i64() || j.is_u64(), "a whole number"),
            DataType::Float => fits(j.is_number(), "a number"),
            DataType::Bool => fits(j.is_boolean(), "true or false"),
            DataType::String => fits(j.is_string(), "text in quotes"),
            DataType::Array(elem) => {
                let Some(items) = j.as_array() else { return Some(String::from("a list, [ ... ]")) };
                let elem = elem.as_deref()?;
                items.iter().find_map(|e| self.json_needs(e, elem).map(|n| format!("a list of {}", n)))
            }
            DataType::Map(kv) => {
                let Some(map) = j.as_object() else { return Some(String::from("a table")) };
                let elem = kv.1.as_ref()?;
                map.values().find_map(|e| self.json_needs(e, elem).map(|n| format!("a table of {n}")))
            }
            DataType::Struct(id) => {
                let info = &self.table.structs[*id as usize];
                let fields: Vec<&str> = info.fields.iter().map(|(n, _)| n.as_str()).collect();
                let table = || format!("a table with {}, as {{ {} }}", fields.join(", "), fields.iter().map(|f| format!("{f} = ...")).collect::<Vec<_>>().join(", "));
                let Some(obj) = j.as_object() else { return Some(table()) };
                if obj.keys().any(|k| !fields.contains(&k.as_str())) {
                    return Some(table());
                }
                info.fields.iter().find_map(|(f, t)| obj.get(f).and_then(|v| self.json_needs(v, t)).map(|n| format!("{f} to be {n}")))
            }
            DataType::Enum(id) => {
                let info = &self.table.enums[*id as usize];
                let ok = j.as_str().is_some_and(|n| info.variants.iter().any(|v| v == n));
                fits(ok, &format!("one of {}", info.variants.iter().map(|v| format!("\"{v}\"")).collect::<Vec<_>>().join(", ")))
            }
            _ => None,
        }
    }

    fn merge(&self, base: &Value, j: &Json, id: u16) -> Value {
        let Value::Struct { name, fields } = base else { return base.clone() };
        let info = &self.table.structs[id as usize];
        let Some(obj) = j.as_object() else { return base.clone() };
        let fields = fields
            .iter()
            .map(|(f, v)| match (obj.get(f), info.fields.iter().find(|(n, _)| n == f)) {
                (Some(jv), Some((_, t))) => (f.clone(), self.from_json(jv, t)),
                _ => (f.clone(), v.clone()),
            })
            .collect();
        Value::Struct { name: name.clone(), fields }
    }
}

// ---- Systems ----

#[derive(Clone, Debug)]
enum Param {
    /// Rows of components; `entity` positions get the entity's id.
    Rows { row: u16, fields: Vec<(String, Option<String>)>, with: Vec<String>, without: Vec<String> },
    /// `key` names the component for the lookup functions.
    Lookup { name: u16, key: i64 },
    Resource(String),
    Messages(String),
    Contacts { name: u16, kind: i32, a: String, b: String },
    Commands(u16),
    Time(u16),
    Engine(u16),
    /// The row of the entity this call is for: a tuple of its components,
    /// with the entity's id at `Entity` places.
    Row { row: u16, fields: Vec<(String, Option<String>)> },
    /// The contact or overlap this call is for.
    OneContact { name: u16, kind: i32, a: String, b: String },
    /// The message this call is for.
    OneMessage(String),
    /// The current state of a @drift::state enum, as `State<E>`.
    State { name: u16, of: String },
}

#[derive(Clone, Debug)]
struct System {
    name: String,
    params: Vec<Param>,
    run_if: Vec<String>,
    /// Runs only while a state is one of these: (enum, variant).
    in_state: Vec<(String, String)>,
    /// Runs once when a state becomes, or stops being, this variant.
    on_enter: Option<(String, String)>,
    on_exit: Option<(String, String)>,
    startup: bool,
    enabled: bool,
    after: Vec<String>,
    before: Vec<String>,
    /// Messages up to this sequence number have been delivered.
    seen: u64,
}

/// What one call of a system is about.
enum Focus {
    None,
    Entity(u64),
    /// a, b, point and normal, and whether a and b were swapped from the
    /// engine's order (the normal then points the other way).
    Contact(u64, u64, [f64; 4], bool),
    Message(Value),
}

enum Command {
    Spawn(u64, Vec<Value>),
    Insert(u64, Value),
    Remove(u64, String),
    Despawn(u64),
    InsertResource(Value),
    RemoveResource(String),
    Send(Value),
    /// Turns a system on or off by function name.
    Switch(String, bool),
    /// Starts the game over once this frame's systems have run.
    Restart,
    /// A message delivered once this many seconds have passed.
    SendAfter(f64, Value),
    /// Writes a scene file: every entity, or `root` and its children.
    SaveScene(String, u64),
}

thread_local! {
    /// The names of the game's components, for the engine to ask about
    /// while a system runs.
    static COMPONENT_NAMES: RefCell<HashSet<String>> = RefCell::new(HashSet::new());
    /// Script components a scene spawned: entity, component, its JSON, and
    /// the file's entity ids mapped to the new entities.
    static SCENE_COMPONENTS: RefCell<Vec<(u64, String, String, String)>> = const { RefCell::new(Vec::new()) };
    /// Entities the game spawned, for a restart to remove.
    static SPAWNED: RefCell<BTreeSet<u64>> = const { RefCell::new(BTreeSet::new()) };
    /// Whether each system is on, as of the start of this system's call.
    static SWITCHES: RefCell<HashMap<String, bool>> = RefCell::new(HashMap::new());
    static QUEUE: RefCell<Vec<Command>> = const { RefCell::new(Vec::new()) };
    static HOST: RefCell<Option<Host>> = const { RefCell::new(None) };
    static ERROR: RefCell<CString> = RefCell::new(CString::default());
    /// The last drift_host_check answer.
    static CHECKED: RefCell<CString> = RefCell::new(CString::default());
    /// The game's types, for host functions that build values of a type the
    /// script names.
    static TYPES: RefCell<Option<std::rc::Rc<Types>>> = const { RefCell::new(None) };
    /// The script's resources by bare name, as of the last change, for
    /// `drift::resource_text` while a system runs.
    static RESOURCE_VIEW: RefCell<HashMap<String, Value>> = RefCell::new(HashMap::new());
    /// Script components: type name -> entity -> value. Lookups read them
    /// while a system runs.
    static STORES: RefCell<HashMap<String, BTreeMap<u64, Value>>> = RefCell::new(HashMap::new());
    /// The component each lookup key reads.
    static LOOKUP_NAMES: RefCell<Vec<String>> = const { RefCell::new(Vec::new()) };
    /// What each lookup handed out during the current call, by (key,
    /// entity), to tell what the system changed.
    static LOOKUP_SEEN: RefCell<HashMap<(i64, u64), Value>> = RefCell::new(HashMap::new());
    /// Whether a load may start from the bytecode beside the entry: the
    /// first load of a page does; hot reloads and checks compile the
    /// scripts.
    static USE_BUILT: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
    /// The last drift_host_build answer.
    static BUILT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
    /// is_engine's answers by component name.
    static ENGINE_COMPONENTS: RefCell<HashMap<String, bool>> = RefCell::new(HashMap::new());
}

/// Entity `e`'s component `name`: from the script's stores, or the engine.
fn read_component(e: u64, name: &str) -> Option<Value> {
    if let Some(found) = STORES.with(|s| s.borrow().get(name).map(|store| store.get(&e).cloned())) {
        return found;
    }
    TYPES.with(|t| {
        let t = t.borrow();
        let types = t.as_ref()?;
        let id = types.struct_id(name)?;
        engine_get(e, name, types, id)
    })
}

/// Whether entity `e` has component `name`.
fn has_component(e: u64, name: &str) -> bool {
    if let Some(found) = STORES.with(|s| s.borrow().get(name).map(|store| store.contains_key(&e))) {
        return found;
    }
    let c = cstr(name);
    unsafe { drift_bridge_has(e, c.as_ptr()) != 0 }
}

/// The entities with component `name`, in spawn order.
fn entities_with(name: &str) -> Vec<u64> {
    if let Some(found) = STORES.with(|s| s.borrow().get(name).map(|store| store.keys().copied().collect())) {
        return found;
    }
    engine_list(name)
}

fn lookup_name(key: i64) -> Option<String> {
    LOOKUP_NAMES.with(|l| l.borrow().get(key as usize).cloned())
}

fn publish_resource(name: &str, v: Option<&Value>) {
    RESOURCE_VIEW.with(|r| {
        let mut r = r.borrow_mut();
        match v {
            Some(v) => r.insert(bare(name).to_owned(), v.clone()),
            None => r.remove(bare(name)),
        };
    });
}

/// The field a path like `Game.score` or `Game.player.hp` names in the
/// script's resources, as text: numbers and bools as written, strings as
/// they are, "" when the path names nothing.
fn resource_text(path: &str) -> String {
    let mut parts = path.split('.');
    let Some(first) = parts.next() else { return String::new() };
    RESOURCE_VIEW.with(|r| {
        let r = r.borrow();
        let Some(mut v) = r.get(bare(first)) else { return String::new() };
        for p in parts {
            match field(v, p) {
                Some(next) => v = next,
                None => return String::new(),
            }
        }
        match v {
            Value::Int(i) => i.to_string(),
            Value::Float(f) => f.to_string(),
            Value::Bool(b) => b.to_string(),
            Value::String(s) => s.clone(),
            other => to_json(other).to_string(),
        }
    })
}

/// Checks that `ty` is a resource and `path` ("score", "player.hp") names a
/// field of it, for `resource_field<T>(path)`. The error names the type and
/// lists the fields it has.
fn resource_field_check(ty: &str, path: &str) -> Result<(), String> {
    TYPES.with(|t| {
        let t = t.borrow();
        let Some(types) = t.as_ref() else { return Ok(()) };
        let Some(DataType::Struct(id)) = types.parse_type(ty) else {
            return Err(format!("drift_not_a_resource: {ty} is not a struct; resource_field<T> takes a struct marked @resource or @settings"));
        };
        let marked = |attr: &str| types.table.named("drift", attr).any(|a| matches!(a.target, AttributeTarget::Struct(s) if s == id));
        let name = bare(types.struct_name(id)).to_owned();
        if !marked("resource") && !marked("settings") {
            let what = if marked("component") {
                "a component"
            } else if marked("message") {
                "a message"
            } else {
                "a struct without @resource"
            };
            return Err(format!(
                "drift_not_a_resource: {name} is {what}, not a resource; resource_field<T> takes a struct marked @resource or @settings"
            ));
        }
        let fields_of = |id: u16| types.table.structs[id as usize].fields.iter().map(|(f, _)| f.as_str()).collect::<Vec<_>>().join(", ");
        let mut at = id;
        let mut walked = name;
        let parts: Vec<&str> = path.split('.').collect();
        for (i, part) in parts.iter().enumerate() {
            let info = &types.table.structs[at as usize];
            let Some((_, field_type)) = info.fields.iter().find(|(f, _)| f == part) else {
                return Err(format!("drift_no_such_field: {walked} has no field \"{part}\"; its fields are {}", fields_of(at)));
            };
            if let Some(next) = parts.get(i + 1) {
                let DataType::Struct(inner) = field_type else {
                    return Err(format!("drift_no_such_field: {walked}.{part} is not a struct, so it has no field \"{next}\""));
                };
                at = *inner;
                walked = format!("{walked}.{part}");
            }
        }
        Ok(())
    })
}

/// "" for one of the game's systems; otherwise the error the module raises,
/// listing the systems there are.
fn system_error(name: &str) -> String {
    if SWITCHES.with(|w| w.borrow().contains_key(name)) {
        return String::new();
    }
    let mut names: Vec<String> = SWITCHES.with(|w| w.borrow().keys().cloned().collect());
    names.sort();
    format!("drift_unknown_system: no system \"{name}\"; the systems are {}", names.join(", "))
}

/// The error the module raises for an input action the game never declared,
/// listing the ones it has.
fn action_error(name: &str) -> String {
    let mut names: Vec<String> =
        call("action_names", json!([])).as_array().map(|a| a.iter().filter_map(|n| n.as_str().map(str::to_owned)).collect()).unwrap_or_default();
    names.sort();
    let known = if names.is_empty() { String::from("none") } else { names.join(", ") };
    format!(
        "drift_unknown_action: no input action \"{name}\"; the actions are {known}. Declare actions in drift.toml [input] or with the Input.bind methods"
    )
}

fn queue(c: Command) {
    QUEUE.with(|q| q.borrow_mut().push(c));
}

struct Host {
    program: Prog,
    types: std::rc::Rc<Types>,
    systems: Vec<System>,
    components: HashSet<String>,
    messages_kinds: HashSet<String>,
    resource_kinds: HashSet<String>,
    resources: HashMap<String, Value>,
    /// Counts the writes to `resources`, so a copy of one can be reused
    /// while nothing has written it.
    resources_version: u64,
    messages: Vec<(u64, String, Value)>,
    /// Messages sent with a delay: seconds left, then the message.
    later: Vec<(f64, Value)>,
    next_seq: u64,
    /// This frame's collisions: kind, a, b, and for contacts that began the
    /// first point and the normal from a to b.
    contacts: Vec<(i32, u64, u64, [f64; 4])>,
    started: bool,
    reported: HashSet<String>,
    /// run_if conditions by function name, as systems with parameters.
    conditions: HashMap<String, System>,
    condition_cache: HashMap<String, bool>,
    /// Components a component brings with it: type -> required types.
    requires: HashMap<String, Vec<String>>,
    /// required_of's answers.
    required: HashMap<String, std::rc::Rc<Vec<String>>>,
    /// Script components built from their defaults, for requires.
    component_defaults: HashMap<String, Value>,
    /// Messages built from their defaults, for reloads.
    message_defaults: HashMap<String, Value>,
    /// @drift::state enums by name.
    states: HashMap<String, StateNow>,
    /// This frame's time scale and unscaled frame time.
    time_scale: (f64, f64),
    /// What a restart puts back: resources as they were built, and whether
    /// each system was on.
    initial_resources: HashMap<String, Value>,
    initial_enabled: Vec<bool>,
    restart_pending: bool,
    /// Systems turned off because they raised an error; a reload with new
    /// code turns them on again.
    failed: HashSet<String>,
    /// After a reload: startup systems the old program did not have, which
    /// run once at the next frame, and states entered without their
    /// on_enter systems having run.
    pending_startup: Vec<String>,
    pending_enter: Vec<(String, String)>,
}

/// The game's program: compiled here from its scripts, or loaded from the
/// bytecode `drift build --web` compiled them to (see `built_program`).
enum Prog {
    Source(Program),
    /// The program, and the name each function a system or a condition
    /// names is exported under.
    Built(RuntimeProgram, HashMap<String, String>),
}

/// An error a call raised: its kind and its message.
struct Raised {
    code: String,
    message: String,
}

impl From<Diagnostic> for Raised {
    fn from(d: Diagnostic) -> Self {
        Raised { code: d.code, message: d.message }
    }
}

impl From<candela::CallError> for Raised {
    fn from(e: candela::CallError) -> Self {
        match e {
            candela::CallError::Runtime(d) => d.into(),
            other => Raised { code: String::from("host_call"), message: other.to_string() },
        }
    }
}

impl Prog {
    fn call(&mut self, name: &str, args: &[Value]) -> Result<Value, Raised> {
        match self {
            Prog::Source(p) => p.call(name, args).map_err(Raised::from),
            Prog::Built(p, exports) => p.call(exports.get(name).map_or(name, String::as_str), args).map_err(Raised::from),
        }
    }

    fn call_in_out(&mut self, name: &str, args: &mut [Value]) -> Result<Value, Raised> {
        match self {
            Prog::Source(p) => p.call_in_out(name, args).map_err(Raised::from),
            Prog::Built(p, exports) => p.call_in_out(exports.get(name).map_or(name, String::as_str), args).map_err(Raised::from),
        }
    }

    fn collect(&mut self, budget: u32) -> bool {
        match self {
            Prog::Source(p) => p.collect(budget),
            Prog::Built(p, _) => p.collect(budget),
        }
    }
}

struct StateNow {
    now: String,
    since: f64,
    /// Asked for with `state.set(...)`; applied after the Update systems.
    next: Option<String>,
}

/// The names of the structs a list of attribute type values names.
fn type_list(types: &Types, v: Option<&AttributeValue>) -> Vec<String> {
    v.and_then(AttributeValue::as_list)
        .unwrap_or(&[])
        .iter()
        .filter_map(|t| match t {
            AttributeValue::Type(DataType::Struct(id)) => Some(types.struct_name(*id).to_owned()),
            _ => None,
        })
        .collect()
}

fn fn_list(v: Option<&AttributeValue>) -> Vec<String> {
    v.and_then(AttributeValue::as_list)
        .unwrap_or(&[])
        .iter()
        .filter_map(|t| match t {
            AttributeValue::Fn { name, .. } => Some(name.clone()),
            _ => None,
        })
        .collect()
}

/// `Name<A, B>` -> ("Name", ["A", "B"]), with any `module::` prefix dropped
/// from the name.
fn generic(name: &str) -> Option<(&str, Vec<String>)> {
    let open = name.find('<')?;
    let base = name[..open].rsplit("::").next().unwrap_or(&name[..open]);
    let inner = &name[open + 1..name.len() - 1];
    let mut args = Vec::new();
    let mut depth = 0;
    let mut start = 0;
    for (i, ch) in inner.char_indices() {
        match ch {
            '<' | '(' => depth += 1,
            '>' | ')' => depth -= 1,
            ',' if depth == 0 => {
                args.push(inner[start..i].trim().to_owned());
                start = i + 1;
            }
            _ => {}
        }
    }
    args.push(inner[start..].trim().to_owned());
    Some((base, args))
}

fn bare(name: &str) -> &str {
    name.rsplit("::").next().unwrap_or(name)
}

impl Host {
    fn is_component(&self, name: &str) -> bool {
        self.components.contains(name) || is_engine(name)
    }

    fn classify(&self, ty: &DataType) -> Result<Param, String> {
        let types = &self.types;
        match ty {
            DataType::Array(Some(elem)) => {
                let DataType::Struct(id) = **elem else { return Err(format!("a list of {}", types.table.type_name(elem))) };
                let name = types.struct_name(id).to_owned();
                if self.messages_kinds.contains(&name) {
                    return Ok(Param::Messages(name));
                }
                if let Some((base, args)) = generic(&name) {
                    let kind = match base {
                        "Contact" => 0,
                        "ContactEnd" => 1,
                        "Overlap" => 2,
                        "OverlapEnd" => 3,
                        _ => -1,
                    };
                    if kind >= 0 && args.len() == 2 {
                        return Ok(Param::Contacts { name: id, kind, a: args[0].clone(), b: args[1].clone() });
                    }
                }
                // A tuple, or a @drift::query row struct.
                let fields = self.row_fields(id)?;
                let (with, without) = types
                    .table
                    .named("drift", "query")
                    .find(|a| matches!(a.target, AttributeTarget::Struct(s) if s == id))
                    .map(|a| (type_list(types, a.arg("with")), type_list(types, a.arg("without"))))
                    .unwrap_or_default();
                Ok(Param::Rows { row: id, fields, with, without })
            }
            DataType::Struct(id) => {
                let name = types.struct_name(*id).to_owned();
                if self.resource_kinds.contains(&name) {
                    return Ok(Param::Resource(name));
                }
                if let Some((base, args)) = generic(&name) {
                    if base == "Lookup" && args.len() == 1 {
                        let key = LOOKUP_NAMES.with(|l| {
                            let mut l = l.borrow_mut();
                            match l.iter().position(|n| *n == args[0]) {
                                Some(i) => i,
                                None => {
                                    l.push(args[0].clone());
                                    l.len() - 1
                                }
                            }
                        });
                        return Ok(Param::Lookup { name: *id, key: key as i64 });
                    }
                    if base == "State" && args.len() == 1 {
                        let of = args[0].clone();
                        if !self.states.contains_key(bare(&of)) {
                            return Err(format!("{of} is not a @drift::state enum"));
                        }
                        return Ok(Param::State { name: *id, of: bare(&of).to_owned() });
                    }
                }
                if let Some((base, args)) = generic(&name) {
                    let kind = match base {
                        "Contact" => 0,
                        "ContactEnd" => 1,
                        "Overlap" => 2,
                        "OverlapEnd" => 3,
                        _ => -1,
                    };
                    if kind >= 0 && args.len() == 2 {
                        return Ok(Param::OneContact { name: *id, kind, a: args[0].clone(), b: args[1].clone() });
                    }
                }
                if self.messages_kinds.contains(&name) {
                    return Ok(Param::OneMessage(name));
                }
                // A tuple: the row of the entity each call is for.
                if name.starts_with('(') {
                    return Ok(Param::Row { row: *id, fields: self.row_fields(*id)? });
                }
                match bare(&name) {
                    "Commands" => Ok(Param::Commands(*id)),
                    "Time" => Ok(Param::Time(*id)),
                    "Input" | "Assets" | "Window" | "Effects" | "Save" | "Random" | "Camera2D" | "Draw" => {
                        Ok(Param::Engine(*id))
                    }
                    // The gameplay half's engine resources.
                    "Physics" | "Audio" | "Systems" | "Spatial" => Ok(Param::Engine(*id)),
                    // The browser page and the platform.
                    "Platform" => Ok(Param::Engine(*id)),
                    _ => Err(format!("{name} is not a resource, a query, a lookup or an engine resource")),
                }
            }
            other => Err(format!("{} is not a system parameter", types.table.type_name(other))),
        }
    }

    /// The places of a row type (a tuple or a @drift::query struct), each
    /// with the component it holds, or None for an `Entity`.
    fn row_fields(&self, id: u16) -> Result<Vec<(String, Option<String>)>, String> {
        let types = &self.types;
        let info = &types.table.structs[id as usize];
        let mut fields = Vec::new();
        for (f, t) in &info.fields {
            let DataType::Struct(cid) = t else {
                return Err(format!("{} in {} is not a component", types.table.type_name(t), info.name));
            };
            let cname = types.struct_name(*cid).to_owned();
            if bare(&cname) == "Entity" {
                fields.push((f.clone(), None));
            } else if self.is_component(&cname) {
                fields.push((f.clone(), Some(cname)));
            } else {
                return Err(format!("{cname} in {} is not a component", info.name));
            }
        }
        if fields.iter().all(|(_, c)| c.is_none()) {
            return Err(format!("{} names no component", info.name));
        }
        Ok(fields)
    }

    /// The row `row` of entity `e`, or None when it lacks a component.
    fn row_value(&self, row: u16, fields: &[(String, Option<String>)], e: u64) -> Option<Value> {
        let mut values = Vec::new();
        for (f, c) in fields {
            let v = match c {
                None => self.entity_value(e),
                Some(c) => self.component(e, c)?,
            };
            values.push((f.clone(), v));
        }
        Some(Value::Struct { name: self.types.struct_name(row).to_owned(), fields: values })
    }

    /// Notes the components a system changed in one row of entity `e`: a
    /// game component against its store, an engine component against the
    /// bytes it was read as (see `kept`). An engine component that changed
    /// goes with its new bytes, so it is not written out twice.
    fn diff_row(fields: &[(String, Option<String>)], old: Option<&KeptRow>, new: Option<&mut Value>, e: u64, writes: &mut Vec<Write>) {
        let (Some(old), Some(Value::Struct { fields: nf, .. })) = (old, new) else { return };
        for (k, (_, c)) in fields.iter().enumerate() {
            let Some(c) = c else { continue };
            match script_unchanged(c, e, &nf[k].1) {
                Some(true) => {}
                Some(false) => writes.push((e, std::mem::replace(&mut nf[k].1, Value::Null), None)),
                None => {
                    let mut bytes = Vec::with_capacity(old.get(k).and_then(Option::as_ref).map_or(64, Vec::len));
                    wire::write_value(&mut bytes, &nf[k].1);
                    if old.get(k).and_then(Option::as_ref) != Some(&bytes) {
                        writes.push((e, std::mem::replace(&mut nf[k].1, Value::Null), Some(bytes)));
                    }
                }
            }
        }
    }

    /// The error for a system written in the old per-entity form, components
    /// (or Entity) as separate parameters, with the row it should take.
    fn loose_components(&self, system: &str, params: &[(String, DataType)]) -> Option<String> {
        let loose: Vec<(&str, String)> = params
            .iter()
            .filter_map(|(p, t)| match t {
                DataType::Struct(id) => {
                    let n = self.types.struct_name(*id);
                    (bare(n) == "Entity" || self.is_component(n)).then(|| (p.as_str(), bare(n).to_owned()))
                }
                _ => None,
            })
            .collect();
        if loose.is_empty() {
            return None;
        }
        let written: Vec<String> = loose.iter().map(|(p, t)| format!("{p}: {t}")).collect();
        let (mut types, mut names): (Vec<String>, Vec<String>) =
            loose.iter().map(|(p, t)| (t.clone(), (*p).to_owned())).unzip();
        // A tuple holds at least two places: one component goes with its Entity.
        if types.len() == 1 {
            if types[0] == "Entity" {
                types.push("Marker".into());
                names.push("_".into());
            } else {
                types.insert(0, "Entity".into());
                names.insert(0, "_".into());
            }
        }
        let (what, them) = if loose.len() == 1 {
            ("on its own", ", Entity and a component together,")
        } else {
            ("as separate parameters", "")
        };
        Some(format!(
            "drift_loose_components: {system} takes {} {what}. A system that runs once per entity takes one row{them} as a tuple: fn {system}(row: ({}), ...) and inside it let ({}) = row;",
            written.join(", "),
            types.join(", "),
            names.join(", ")
        ))
    }

    fn component(&self, e: u64, name: &str) -> Option<Value> {
        read_component(e, name)
    }

    fn has(&self, e: u64, name: &str) -> bool {
        has_component(e, name)
    }

    fn entities_with(&self, name: &str) -> Vec<u64> {
        entities_with(name)
    }

    fn set_component(&mut self, e: u64, value: Value) {
        let Value::Struct { name, .. } = &value else { return };
        let name = name.clone();
        self.insert_component(e, &name, value);
        self.add_required(e, &name);
    }

    /// The components `name` requires, by the game's names for them: what
    /// its @drift::component attribute lists, and for an engine component
    /// what the engine says it needs. Worked out once per name.
    fn required_of(&mut self, name: &str) -> std::rc::Rc<Vec<String>> {
        if let Some(r) = self.required.get(name) {
            return r.clone();
        }
        let mut required = self.requires.get(name).cloned().unwrap_or_default();
        if is_engine(name) {
            let c = cstr(name);
            if let Some(list) = from_c(unsafe { drift_bridge_requires(c.as_ptr()) })
                .and_then(|t| serde_json::from_str::<Vec<String>>(&t).ok())
            {
                required.extend(list);
            }
        }
        let resolved: Vec<String> = required
            .iter()
            .map(|r| self.types.table.structs.iter().find(|s| bare(&s.name) == bare(r)).map_or(r.clone(), |s| s.name.clone()))
            .collect();
        let resolved = std::rc::Rc::new(resolved);
        self.required.insert(name.to_owned(), resolved.clone());
        resolved
    }

    /// Adds the components `name` requires that `e` lacks, from their defaults.
    fn add_required(&mut self, e: u64, name: &str) {
        let required = self.required_of(name);
        for r in required.iter() {
            let r = r.clone();
            if self.has(e, &r) {
                continue;
            }
            if let Some(v) = self.component_defaults.get(&r).cloned() {
                self.insert_component(e, &r, v);
            } else if is_engine(&r) {
                engine_set_json(e, &r, &json!({}));
            } else {
                continue;
            }
            self.add_required(e, &r);
        }
    }

    fn insert_component(&mut self, e: u64, name: &str, value: Value) {
        let name = name.to_owned();
        if self.components.contains(&name) {
            STORES.with(|s| s.borrow_mut().entry(name).or_default().insert(e, value));
        } else if is_engine(&name) {
            engine_set(e, &name, &value);
        } else if self.reported.insert(format!("bundle:{name}")) {
            log(1, &format!("{name} is not a component: mark it @drift::component"));
        }
    }

    /// A Contact<A, B> (or ContactEnd, Overlap, OverlapEnd) value: the two
    /// entities, and where the struct has them, the point and normal.
    fn contact_value(&self, id: u16, a: u64, b: u64, pn: [f64; 4], swapped: bool) -> Value {
        let s = if swapped { -1.0 } else { 1.0 };
        let j = json!({
            "a": { "id": a }, "b": { "id": b },
            "point": { "x": pn[0], "y": pn[1] },
            "normal": { "x": pn[2] * s, "y": pn[3] * s },
        });
        self.types.from_json(&j, &DataType::Struct(id))
    }

    fn entity_value(&self, e: u64) -> Value {
        let name = self.types.table.structs.iter().find(|s| bare(&s.name) == "Entity").map_or("Entity".into(), |s| s.name.clone());
        Value::Struct { name, fields: vec![("id".into(), Value::Int(e as i64))] }
    }

    /// The entities with every component, in spawn order.
    fn matching(&self, components: &[String], without: &[String]) -> Vec<u64> {
        let Some((first, rest)) = components.split_first() else { return Vec::new() };
        let mut set: BTreeSet<u64> = self.entities_with(first).into_iter().collect();
        for c in rest {
            set.retain(|e| self.has(*e, c));
        }
        for c in without {
            set.retain(|e| !self.has(*e, c));
        }
        set.into_iter().collect()
    }

    fn engine_resource(&self, id: u16) -> Value {
        let name = self.types.struct_name(id).to_owned();
        let fields = self.types.table.structs[id as usize]
            .fields
            .iter()
            .map(|(f, t)| {
                let v = match (bare(&name), f.as_str()) {
                    ("Window", "width") => Value::Float(call("window_size", json!([]))[0].as_f64().unwrap_or(0.0)),
                    ("Window", "height") => Value::Float(call("window_size", json!([]))[1].as_f64().unwrap_or(0.0)),
                    // Immediate draws go over entities unless a system asks otherwise.
                    ("Draw", "z") => Value::Float(1000.0),
                    _ => self.types.from_json(&Json::Null, t),
                };
                (f.clone(), v)
            })
            .collect();
        Value::Struct { name, fields }
    }

    /// Builds the arguments of one system and what each row came from.
    fn arguments(&self, sys: &System, time: (f64, f64, i64), focus: &Focus) -> Option<(Vec<Value>, Vec<Vec<u64>>)> {
        self.arguments_reusing(sys, time, focus, Vec::new())
    }

    /// arguments, taking resources from `reuse` (by parameter) when it
    /// holds them: what the call before left unchanged, so a system that
    /// runs once per entity does not copy each resource once per entity.
    fn arguments_reusing(
        &self,
        sys: &System,
        time: (f64, f64, i64),
        focus: &Focus,
        mut reuse: Vec<Option<Value>>,
    ) -> Option<(Vec<Value>, Vec<Vec<u64>>)> {
        let mut args = Vec::with_capacity(sys.params.len());
        let mut entities = Vec::with_capacity(sys.params.len());
        for (i, p) in sys.params.iter().enumerate() {
            let mut ids = Vec::new();
            let v = match p {
                Param::Rows { row, fields, with, without } => {
                    let mut needed: Vec<String> = fields.iter().filter_map(|(_, c)| c.clone()).collect();
                    needed.extend(with.iter().cloned());
                    let mut rows = Vec::new();
                    for e in self.matching(&needed, without) {
                        rows.push(self.row_value(*row, fields, e)?);
                        ids.push(e);
                    }
                    Value::Array(rows)
                }
                // Empty: `get` fetches what the system reads (lookup_get).
                Param::Lookup { name, key } => Value::Struct {
                    name: self.types.struct_name(*name).to_owned(),
                    fields: vec![
                        ("key".into(), Value::Int(*key)),
                        ("ids".into(), Value::Array(Vec::new())),
                        ("rows".into(), Value::Array(Vec::new())),
                        ("slots".into(), Value::Map(BTreeMap::new())),
                    ],
                },
                Param::Resource(name) => match reuse.get_mut(i).and_then(Option::take) {
                    Some(v) => v,
                    None => self.resources.get(name)?.clone(),
                },
                Param::Messages(name) => Value::Array(
                    self.messages.iter().filter(|(seq, n, _)| *seq > sys.seen && n == name).map(|(_, _, v)| v.clone()).collect(),
                ),
                Param::Contacts { name, kind, a, b } => {
                    let row = self.types.struct_name(*name).to_owned();
                    let mut out = Vec::new();
                    for &(k, x, y, pn) in &self.contacts {
                        if k != *kind {
                            continue;
                        }
                        if self.has(x, a) && self.has(y, b) {
                            out.push(self.contact_value(*name, x, y, pn, false));
                        } else if self.has(y, a) && self.has(x, b) {
                            out.push(self.contact_value(*name, y, x, pn, true));
                        }
                    }
                    let _ = row;
                    Value::Array(out)
                }
                Param::Commands(id) => Value::Struct { name: self.types.struct_name(*id).to_owned(), fields: vec![("id".into(), Value::Int(0))] },
                Param::Time(id) => Value::Struct {
                    name: self.types.struct_name(*id).to_owned(),
                    fields: vec![
                        ("dt".into(), Value::Float(time.0)),
                        ("elapsed".into(), Value::Float(time.1)),
                        ("frame".into(), Value::Int(time.2)),
                        ("scale".into(), Value::Float(self.time_scale.0)),
                        ("real_dt".into(), Value::Float(self.time_scale.1)),
                    ],
                },
                Param::Engine(id) => self.engine_resource(*id),
                Param::Row { row, fields } => {
                    let Focus::Entity(e) = focus else { return None };
                    ids.push(*e);
                    self.row_value(*row, fields, *e)?
                }
                Param::OneContact { name, .. } => {
                    let Focus::Contact(a, b, pn, swapped) = focus else { return None };
                    self.contact_value(*name, *a, *b, *pn, *swapped)
                }
                Param::OneMessage(_) => {
                    let Focus::Message(v) = focus else { return None };
                    v.clone()
                }
                Param::State { name, of } => {
                    let st = self.states.get(of)?;
                    let now = Value::Enum { variant: st.now.clone(), payload: Vec::new() };
                    Value::Struct {
                        name: self.types.struct_name(*name).to_owned(),
                        fields: vec![
                            ("now".into(), now.clone()),
                            ("since".into(), Value::Float(st.since)),
                            ("next".into(), now),
                            ("changing".into(), Value::Bool(false)),
                        ],
                    }
                }
            };
            args.push(v);
            entities.push(ids);
        }
        Some((args, entities))
    }

    /// Keeps every change the system made to components and resources.
    /// `before` holds what the call was given for the parameters this can
    /// write back (see `kept`), `after` what the call left; values are
    /// taken out of `after`.
    /// Every change is found first, against the stores and resources as
    /// they were during the call, then kept in parameter order, so a
    /// component two parameters hold is written only by those that changed
    /// it.
    fn write_back(&mut self, sys: &System, before: &[Vec<KeptRow>], after: &mut [Value], entities: &[Vec<u64>]) {
        let mut writes: Vec<Write> = Vec::new();
        let mut resources: Vec<(String, Value)> = Vec::new();
        for (i, p) in sys.params.iter().enumerate() {
            let (old, new) = (&before[i], &mut after[i]);
            match p {
                Param::Rows { fields, .. } => {
                    let Value::Array(n) = new else { continue };
                    for (r, e) in entities[i].iter().enumerate() {
                        Self::diff_row(fields, old.get(r), n.get_mut(r), *e, &mut writes);
                    }
                }
                Param::Row { fields, .. } => {
                    if let Some(e) = entities[i].first() {
                        Self::diff_row(fields, old.first(), Some(new), *e, &mut writes);
                    }
                }
                Param::Lookup { key, .. } => {
                    let Some(name) = lookup_name(*key) else { continue };
                    let Value::Struct { fields, .. } = new else { continue };
                    let (Some(ids), Some(rows)) = (fields.iter().position(|(f, _)| f == "ids"), fields.iter().position(|(f, _)| f == "rows")) else { continue };
                    let ids: Vec<u64> = match &fields[ids].1 {
                        Value::Array(ids) => ids.iter().filter_map(|id| id.as_i64()).map(|id| id as u64).collect(),
                        _ => continue,
                    };
                    let Value::Array(rows) = &mut fields[rows].1 else { continue };
                    for (e, row) in ids.into_iter().zip(rows.iter_mut()) {
                        let changed = match script_unchanged(&name, e, row) {
                            Some(same) => !same,
                            None => LOOKUP_SEEN.with(|seen| seen.borrow().get(&(*key, e)).is_some_and(|was| was != row)),
                        };
                        if changed {
                            writes.push((e, std::mem::replace(row, Value::Null), None));
                        }
                    }
                }
                Param::Resource(name) => {
                    if self.resources.get(name) != Some(new) {
                        resources.push((name.clone(), std::mem::replace(new, Value::Null)));
                    }
                }
                Param::State { of, .. } => {
                    if let (Some(Value::Bool(true)), Some(Value::Enum { variant, .. })) = (field(new, "changing"), field(new, "next")) {
                        if let Some(st) = self.states.get_mut(of) {
                            st.next = Some(variant.clone());
                        }
                    }
                }
                _ => {}
            }
        }
        // These are components the entity already has, so what they
        // require is already there.
        for (e, v, bytes) in writes {
            let Value::Struct { name, .. } = &v else { continue };
            match bytes {
                Some(bytes) => engine_set_bytes(e, name, &bytes),
                None => {
                    let name = name.clone();
                    self.insert_component(e, &name, v);
                }
            }
        }
        for (name, v) in resources {
            publish_resource(&name, Some(&v));
            self.resources.insert(name, v);
            self.resources_version += 1;
        }
    }

    /// Puts the script components scenes spawned onto their entities.
    fn apply_scene_components(&mut self) {
        let pending = SCENE_COMPONENTS.with(|q| std::mem::take(&mut *q.borrow_mut()));
        for (e, name, text, ids) in pending {
            let Some(full) = self.components.iter().find(|c| bare(c) == name).cloned() else { continue };
            let Some(id) = self.types.struct_id(&full) else { continue };
            let j: Json = serde_json::from_str(&text).unwrap_or(Json::Null);
            let map: HashMap<String, u64> = serde_json::from_str(&ids).unwrap_or_default();
            let entity = |x: &Json| x.as_i64().and_then(|l| map.get(&l.to_string())).map_or(0, |n| *n as i64);
            let v = self.types.from_json_with(&j, &DataType::Struct(id), Some(&entity));
            SPAWNED.with(|s| s.borrow_mut().insert(e));
            self.set_component(e, v);
        }
    }

    /// Writes a scene file: the engine serializes its components, and the
    /// game's components join their entities with entity references turned
    /// into the file's ids.
    fn save_scene(&mut self, path: &str, root: u64) {
        let mut extra: BTreeSet<u64> = BTreeSet::new();
        if root == 0 {
            STORES.with(|s| {
                for store in s.borrow().values() {
                    extra.extend(store.keys().copied());
                }
            });
        }
        let answer = call("scene_serialize", json!([root, extra.into_iter().collect::<Vec<u64>>()]));
        let (Some(mut doc), Some(order)) = (answer.get("doc").cloned(), answer.get("order").and_then(Json::as_array).cloned()) else {
            log(2, &format!("drift_save_failed: {path}"));
            return;
        };
        let order: Vec<u64> = order.iter().filter_map(Json::as_u64).collect();
        let local: HashMap<u64, i64> = order.iter().enumerate().map(|(i, e)| (*e, i as i64)).collect();
        if let Some(ents) = doc.get_mut("entities").and_then(Json::as_array_mut) {
            for (i, e) in order.iter().enumerate() {
                let Some(comps) = ents.get_mut(i).and_then(|x| x.get_mut("components")).and_then(Json::as_object_mut) else { continue };
                STORES.with(|s| {
                    for (name, store) in s.borrow().iter() {
                        if let Some(v) = store.get(e) {
                            comps.insert(bare(name).to_owned(), scene_json(v, &local));
                        }
                    }
                });
            }
        }
        if !call("scene_write", json!([path, doc.to_string()])).as_bool().unwrap_or(false) {
            log(2, &format!("drift_save_failed: {path}"));
        }
    }

    fn apply(&mut self) {
        self.apply_scene_components();
        let commands = QUEUE.with(|q| std::mem::take(&mut *q.borrow_mut()));
        for c in commands {
            match c {
                Command::Spawn(e, bundle) => {
                    for v in bundle {
                        self.set_component(e, v);
                    }
                }
                Command::Insert(e, v) => self.set_component(e, v),
                Command::Remove(e, name) => {
                    let removed = STORES.with(|s| s.borrow_mut().get_mut(&name).map(|store| store.remove(&e)).is_some());
                    if !removed {
                        let c = cstr(&name);
                        unsafe { drift_bridge_remove(e, c.as_ptr()) };
                    }
                }
                Command::Despawn(e) => {
                    STORES.with(|s| {
                        for store in s.borrow_mut().values_mut() {
                            store.remove(&e);
                        }
                    });
                    unsafe { drift_bridge_despawn(e) };
                }
                Command::InsertResource(v) => {
                    if let Value::Struct { name, .. } = &v {
                        publish_resource(name, Some(&v));
                        self.resources.insert(name.clone(), v.clone());
                        self.resources_version += 1;
                    }
                }
                Command::RemoveResource(name) => {
                    publish_resource(&name, None);
                    self.resources.remove(&name);
                    self.resources_version += 1;
                }
                Command::Switch(name, on) => {
                    for sys in self.systems.iter_mut().filter(|s| s.name == name) {
                        sys.enabled = on;
                    }
                    SWITCHES.with(|w| w.borrow_mut().insert(name, on));
                }
                Command::Restart => self.restart_pending = true,
                Command::SaveScene(path, root) => self.save_scene(&path, root),
                Command::Send(v) => self.deliver(v),
                Command::SendAfter(seconds, v) => self.later.push((seconds, v)),
            }
        }
    }

    fn deliver(&mut self, v: Value) {
        if let Value::Struct { name, .. } = &v {
            self.next_seq += 1;
            self.messages.push((self.next_seq, name.clone(), v.clone()));
        }
    }

    /// Delayed messages whose time has come, and messages the engine sent
    /// since the last frame (`{"type": struct name, "value": fields}`).
    fn receive(&mut self, dt: f64) {
        let mut due = Vec::new();
        self.later.retain_mut(|(left, v)| {
            *left -= dt;
            if *left <= 0.0 {
                due.push(v.clone());
                false
            } else {
                true
            }
        });
        for v in due {
            self.deliver(v);
        }
        let Json::Array(sent) = call("take_messages", json!([])) else { return };
        for m in sent {
            let name = m.get("type").and_then(Json::as_str).unwrap_or("");
            match self.types.parse_type(name) {
                Some(ty @ DataType::Struct(_)) => {
                    let v = self.types.from_json(m.get("value").unwrap_or(&Json::Null), &ty);
                    self.deliver(v);
                }
                _ => {
                    if self.reported.insert(format!("message:{name}")) {
                        log(1, &format!("the engine sent a {name}, which the game does not declare"));
                    }
                }
            }
        }
    }

    /// Whether every run_if condition of the system holds this frame. A
    /// condition is a function over resources; what it changes is discarded.
    fn conditions_hold(&mut self, sys: &System, time: (f64, f64, i64)) -> bool {
        for name in &sys.run_if {
            if let Some(&v) = self.condition_cache.get(name) {
                if !v {
                    return false;
                }
                continue;
            }
            let Some(cond) = self.conditions.get(name).cloned() else { return false };
            let ok = match self.arguments(&cond, time, &Focus::None) {
                Some((mut args, _)) => match self.program.call_in_out(name, &mut args) {
                    Ok(Value::Bool(b)) => b,
                    Ok(_) => {
                        if self.reported.insert(format!("cond:{name}")) {
                            log(2, &format!("run_if: {name} must return bool"));
                        }
                        false
                    }
                    Err(d) => {
                        if self.reported.insert(format!("cond:{name}")) {
                            log(2, &format!("run_if: {name} raised {}: {}", d.code, d.message));
                        }
                        false
                    }
                },
                None => false,
            };
            QUEUE.with(|q| q.borrow_mut().clear());
            self.condition_cache.insert(name.clone(), ok);
            if !ok {
                return false;
            }
        }
        true
    }

    /// The calls a system makes this frame: one, or one per entity that has
    /// every component of its row parameter, or one per event for a single
    /// contact or message parameter.
    fn foci(&self, sys: &System) -> Vec<Focus> {
        for p in &sys.params {
            match p {
                Param::Row { fields, .. } => {
                    let needed: Vec<String> = fields.iter().filter_map(|(_, c)| c.clone()).collect();
                    return self.matching(&needed, &[]).into_iter().map(Focus::Entity).collect();
                }
                Param::OneContact { kind, a, b, .. } => {
                    let mut out = Vec::new();
                    for &(k, x, y, pn) in &self.contacts {
                        if k != *kind {
                            continue;
                        }
                        if self.has(x, a) && self.has(y, b) {
                            out.push(Focus::Contact(x, y, pn, false));
                        } else if self.has(y, a) && self.has(x, b) {
                            out.push(Focus::Contact(y, x, pn, true));
                        }
                    }
                    return out;
                }
                Param::OneMessage(name) => {
                    return self
                        .messages
                        .iter()
                        .filter(|(seq, n, _)| *seq > sys.seen && n == name)
                        .map(|(_, _, v)| Focus::Message(v.clone()))
                        .collect();
                }
                _ => {}
            }
        }
        vec![Focus::None]
    }

    fn run(&mut self, index: usize, time: (f64, f64, i64)) {
        let sys = self.systems[index].clone();
        if !sys.enabled {
            return;
        }
        if !sys.in_state.is_empty()
            && !sys.in_state.iter().any(|(e, v)| self.states.get(e).is_some_and(|s| &s.now == v))
        {
            return;
        }
        if !self.conditions_hold(&sys, time) {
            return;
        }
        let mark = self.next_seq;
        // The resources the last call of this system left unchanged, valid
        // while no resource has been written since.
        let mut stash: Vec<Option<Value>> = Vec::new();
        let mut stash_version = u64::MAX;
        for focus in self.foci(&sys) {
            let reuse = if stash_version == self.resources_version { std::mem::take(&mut stash) } else { Vec::new() };
            let Some((mut args, entities)) = self.arguments_reusing(&sys, time, &focus, reuse) else { continue };
            LOOKUP_SEEN.with(|seen| seen.borrow_mut().clear());
            let before = kept(&sys, &args);
            match self.program.call_in_out(&sys.name, &mut args) {
                Ok(_) => {
                    self.write_back(&sys, &before, &mut args, &entities);
                    self.apply();
                    stash = sys
                        .params
                        .iter()
                        .zip(args.iter_mut())
                        .map(|(p, a)| match p {
                            Param::Resource(_) if !matches!(a, Value::Null) => Some(std::mem::replace(a, Value::Null)),
                            _ => None,
                        })
                        .collect();
                    stash_version = self.resources_version;
                }
                Err(d) => {
                    QUEUE.with(|q| q.borrow_mut().clear());
                    log(2, &format!("{} raised {}: {}", sys.name, d.code, d.message));
                    self.systems[index].enabled = false;
                    self.failed.insert(sys.name.clone());
                    SWITCHES.with(|w| w.borrow_mut().insert(sys.name.clone(), false));
                    break;
                }
            }
        }
        self.systems[index].seen = self.systems[index].seen.max(mark);
    }

    fn run_transition(&mut self, order: &[usize], time: (f64, f64, i64), matches: impl Fn(&System) -> bool) {
        for &i in order {
            if matches(&self.systems[i]) {
                self.run(i, time);
            }
        }
    }

    fn frame(&mut self, time: (f64, f64, i64)) {
        // Components of scenes the engine reloaded between frames.
        self.apply_scene_components();
        self.condition_cache.clear();
        // Entities the engine removed (a Lifetime ran out, a parent went).
        STORES.with(|s| {
            for store in s.borrow_mut().values_mut() {
                store.retain(|e, _| unsafe { drift_bridge_alive(*e) != 0 });
            }
        });
        for st in self.states.values_mut() {
            st.since += time.0;
        }
        self.receive(time.0);
        let order: Vec<usize> = (0..self.systems.len()).collect();
        let is_transition = |s: &System| s.on_enter.is_some() || s.on_exit.is_some();
        if !self.started {
            self.started = true;
            for &i in &order {
                if self.systems[i].startup {
                    self.run(i, time);
                }
            }
            // The first state of each enum is entered once Startup has run.
            let entered: Vec<(String, String)> = self.states.iter().map(|(e, s)| (e.clone(), s.now.clone())).collect();
            for (e, v) in entered {
                self.run_transition(&order, time, |s| s.on_enter.as_ref() == Some(&(e.clone(), v.clone())));
            }
            self.pending_startup.clear();
            self.pending_enter.clear();
        } else {
            // What a reload brought: new startup systems, and states that
            // were entered without their on_enter systems.
            let fresh = std::mem::take(&mut self.pending_startup);
            for &i in &order {
                if self.systems[i].startup && fresh.contains(&self.systems[i].name) {
                    self.run(i, time);
                }
            }
            for (e, v) in std::mem::take(&mut self.pending_enter) {
                self.run_transition(&order, time, |s| s.on_enter.as_ref() == Some(&(e.clone(), v.clone())));
            }
        }
        for &i in &order {
            if !self.systems[i].startup && !is_transition(&self.systems[i]) {
                self.run(i, time);
            }
        }
        // State changes asked for this frame: exit the old, enter the new.
        let changes: Vec<(String, String, String)> = self
            .states
            .iter_mut()
            .filter_map(|(e, s)| {
                let next = s.next.take()?;
                (next != s.now).then(|| (e.clone(), s.now.clone(), next))
            })
            .collect();
        for (e, old, new) in changes {
            self.run_transition(&order, time, |s| s.on_exit.as_ref() == Some(&(e.clone(), old.clone())));
            if let Some(st) = self.states.get_mut(&e) {
                st.now = new.clone();
                st.since = 0.0;
            }
            self.run_transition(&order, time, |s| s.on_enter.as_ref() == Some(&(e.clone(), new.clone())));
        }
        // Every system has run since these were sent, or will not read them.
        let oldest = self.systems.iter().filter(|s| !s.startup).map(|s| s.seen).min().unwrap_or(self.next_seq);
        self.messages.retain(|(seq, _, _)| *seq > oldest);
        self.contacts.clear();
        if self.restart_pending {
            self.restart();
        }
        self.sweep_made_assets();
        self.program.collect(2000);
    }

    /// Lets the engine free the textures and images the game made and no
    /// longer holds, once enough of them have piled up (the engine says
    /// when). Every value the game keeps between frames is here: resources,
    /// components, waiting messages and the defaults things are rebuilt
    /// from; candela has no global variables.
    fn sweep_made_assets(&mut self) {
        if !call("asset_sweep_wanted", json!([])).as_bool().unwrap_or(false) {
            return;
        }
        let mut textures = BTreeSet::new();
        let mut images = BTreeSet::new();
        let mut note = |v: &Value| held_assets(v, &mut textures, &mut images);
        self.resources.values().for_each(&mut note);
        self.initial_resources.values().for_each(&mut note);
        self.component_defaults.values().for_each(&mut note);
        self.message_defaults.values().for_each(&mut note);
        self.messages.iter().for_each(|(_, _, v)| note(v));
        self.later.iter().for_each(|(_, v)| note(v));
        STORES.with(|s| s.borrow().values().for_each(|store| store.values().for_each(&mut note)));
        let freed = call("asset_sweep", json!([textures, images]));
        let freed: Vec<i64> = freed.as_array().map(|a| a.iter().filter_map(Json::as_i64).collect()).unwrap_or_default();
        if freed.iter().any(|n| *n > 0) && self.reported.insert("asset_sweep".into()) {
            log(0, &format!(
                "Candela: freed {} textures and {} images the game made and no longer holds; the engine does this whenever they pile up",
                freed.first().copied().unwrap_or(0),
                freed.get(1).copied().unwrap_or(0)
            ));
        }
    }

    /// Starts the game over: every entity the game spawned goes, resources
    /// and states return to how they started, and the startup systems run
    /// again at the next frame.
    fn restart(&mut self) {
        self.restart_pending = false;
        let mut doomed: BTreeSet<u64> = SPAWNED.with(|s| std::mem::take(&mut *s.borrow_mut()));
        STORES.with(|s| {
            for store in s.borrow_mut().values_mut() {
                doomed.extend(store.keys().copied());
                store.clear();
            }
        });
        for e in doomed {
            unsafe { drift_bridge_despawn(e) };
        }
        self.resources = self.initial_resources.clone();
        self.resources_version += 1;
        for (name, st) in self.states.iter_mut() {
            let first = self
                .types
                .table
                .enums
                .iter()
                .find(|en| bare(&en.name) == name)
                .and_then(|en| en.variants.first().cloned());
            if let Some(first) = first {
                st.now = first;
            }
            st.since = 0.0;
            st.next = None;
        }
        self.messages.clear();
        self.later.clear();
        self.contacts.clear();
        for (sys, on) in self.systems.iter_mut().zip(self.initial_enabled.iter()) {
            sys.enabled = *on;
            sys.seen = 0;
        }
        SWITCHES.with(|w| *w.borrow_mut() = self.systems.iter().map(|s| (s.name.clone(), s.enabled)).collect());
        self.next_seq = 0;
        self.started = false;
        self.failed.clear();
    }
}

/// What write_back compares an engine component of a row against: the
/// component as it was handed to the system, written as MessagePack, which
/// is cheaper to keep and compare than a copy of the value. One entry per
/// place of the row; None for game components (compared against their
/// store, which a call does not change) and entities.
type KeptRow = Vec<Option<Vec<u8>>>;

/// A component a system changed: the entity, the new value, and for an
/// engine component the value's bytes.
type Write = (u64, Value, Option<Vec<u8>>);

/// What write_back compares the arguments of a call against: for each
/// parameter, the kept rows (one for a row parameter, one per entity for a
/// list of rows, none for the rest). A lookup notes what it handed out
/// itself; resources are compared against the host's copy.
fn kept(sys: &System, args: &[Value]) -> Vec<Vec<KeptRow>> {
    let engine_part = |fields: &[(String, Option<String>)], row: &Value| -> KeptRow {
        let Value::Struct { fields: values, .. } = row else { return Vec::new() };
        fields
            .iter()
            .zip(values)
            .map(|((_, c), (_, v))| match c {
                Some(c) if !is_script_component(c) => {
                    let mut bytes = Vec::with_capacity(64);
                    wire::write_value(&mut bytes, v);
                    Some(bytes)
                }
                _ => None,
            })
            .collect()
    };
    sys.params
        .iter()
        .zip(args)
        .map(|(p, a)| match (p, a) {
            (Param::Rows { fields, .. }, Value::Array(rows)) => rows.iter().map(|r| engine_part(fields, r)).collect(),
            (Param::Row { fields, .. }, row) => vec![engine_part(fields, row)],
            _ => Vec::new(),
        })
        .collect()
}

fn is_script_component(name: &str) -> bool {
    STORES.with(|s| s.borrow().contains_key(name))
}

/// For a game component: whether `v` is what entity `e` has stored (an
/// entity without one counts as changed). None for an engine component.
fn script_unchanged(name: &str, e: u64, v: &Value) -> Option<bool> {
    STORES.with(|s| s.borrow().get(name).map(|store| store.get(&e) == Some(v)))
}

/// A game component as a scene file holds it: like to_json, with each
/// Entity as the file's id of the entity, or null when it is not saved.
fn scene_json(v: &Value, local: &HashMap<u64, i64>) -> Json {
    match v {
        Value::Struct { name, fields } if bare(name) == "Entity" => {
            let id = fields.iter().find(|(k, _)| k == "id").and_then(|(_, x)| x.as_i64()).unwrap_or(0);
            local.get(&(id as u64)).map_or(Json::Null, |l| json!(l))
        }
        Value::Struct { fields, .. } => Json::Object(fields.iter().map(|(k, x)| (k.clone(), scene_json(x, local))).collect()),
        Value::Array(items) => Json::Array(items.iter().map(|x| scene_json(x, local)).collect()),
        Value::Map(m) => Json::Object(m.iter().map(|(k, x)| (k.clone(), scene_json(x, local))).collect()),
        other => to_json(other),
    }
}

/// The texture and image ids a value holds, however deep.
fn held_assets(v: &Value, textures: &mut BTreeSet<i64>, images: &mut BTreeSet<i64>) {
    match v {
        Value::Struct { name, fields } => {
            let id = || fields.iter().find(|(f, _)| f == "id").and_then(|(_, x)| x.as_i64());
            match bare(name) {
                "Texture" => textures.extend(id()),
                "Image" => images.extend(id()),
                _ => fields.iter().for_each(|(_, x)| held_assets(x, textures, images)),
            }
        }
        Value::Array(items) => items.iter().for_each(|x| held_assets(x, textures, images)),
        Value::Map(m) => m.values().for_each(|x| held_assets(x, textures, images)),
        Value::Enum { payload, .. } => payload.iter().for_each(|x| held_assets(x, textures, images)),
        _ => {}
    }
}

fn field<'a>(v: &'a Value, name: &str) -> Option<&'a Value> {
    match v {
        Value::Struct { fields, .. } => fields.iter().find(|(n, _)| n == name).map(|(_, v)| v),
        _ => None,
    }
}

/// Orders systems: `after:` and `before:` first, then source order.
fn order(systems: Vec<System>) -> Result<Vec<System>, String> {
    let n = systems.len();
    let index: HashMap<&str, usize> = systems.iter().enumerate().map(|(i, s)| (s.name.as_str(), i)).collect();
    let mut deps: Vec<BTreeSet<usize>> = vec![BTreeSet::new(); n];
    for (i, s) in systems.iter().enumerate() {
        for a in &s.after {
            if let Some(&j) = index.get(a.as_str()) {
                deps[i].insert(j);
            }
        }
        for b in &s.before {
            if let Some(&j) = index.get(b.as_str()) {
                deps[j].insert(i);
            }
        }
    }
    let mut done = vec![false; n];
    let mut out = Vec::new();
    while out.len() < n {
        let next = (0..n).find(|&i| !done[i] && deps[i].iter().all(|&d| done[d]));
        let Some(i) = next else {
            let stuck: Vec<&str> = (0..n).filter(|&i| !done[i]).map(|i| systems[i].name.as_str()).collect();
            return Err(format!("drift_order_cycle: {}", stuck.join(", ")));
        };
        done[i] = true;
        out.push(i);
    }
    Ok(out.into_iter().map(|i| systems[i].clone()).collect())
}

fn line_of(src: &str, offset: usize) -> usize {
    src[..offset.min(src.len())].matches('\n').count() + 1
}

fn report(d: &Diagnostic, entry: &str, src: &str) -> String {
    if d.filename == entry || d.filename.is_empty() {
        format!("{}:{}: {} [{}]", entry, line_of(src, d.span.start), d.message, d.code)
    } else {
        format!("{}: {} [{}]", d.filename, d.message, d.code)
    }
}

fn host_error(e: impl std::fmt::Display) -> HostError {
    HostError::new(e.to_string())
}

/// Where `import "std/..."` reads from: CANDELA_LIB_PATH when set, else the
/// standard library of the installed candela toolchain. The web build carries
/// the standard library inside itself.
fn lib_dir() -> Option<std::path::PathBuf> {
    if cfg!(target_os = "emscripten") || std::env::var_os("CANDELA_LIB_PATH").is_some() {
        return None;
    }
    let home = std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE"))?;
    let dir = std::path::PathBuf::from(home).join(".candela").join("libs");
    dir.is_dir().then_some(dir)
}

fn engine(module_dir: &str) -> Engine {
    let mut e = Engine::new().with_import_root("drift", module_dir);
    if let Some(dir) = lib_dir() {
        e = e.with_lib_dir(dir);
    }
    register_hosts(&mut e);
    e
}

/// Where host functions are registered: the compiler's Engine, or the
/// registry a compiled program binds its host functions from.
trait Hosts {
    fn register_host_fn<Marker, F>(&mut self, namespace: &str, name: &str, f: F)
    where
        F: IntoHostFn<Marker>;
    fn register_host_fn_typed<F>(&mut self, namespace: &str, name: &str, args: Vec<HostType>, ret: HostType, f: F)
    where
        F: Fn(&[Value]) -> Result<Value, HostError> + 'static;
    fn register_host_fn_variadic<F>(&mut self, namespace: &str, name: &str, f: F)
    where
        F: Fn(&[Value]) -> Result<Value, HostError> + 'static;
}

impl Hosts for Engine {
    fn register_host_fn<Marker, F>(&mut self, namespace: &str, name: &str, f: F)
    where
        F: IntoHostFn<Marker>,
    {
        Engine::register_host_fn(self, namespace, name, f);
    }
    fn register_host_fn_typed<F>(&mut self, namespace: &str, name: &str, args: Vec<HostType>, ret: HostType, f: F)
    where
        F: Fn(&[Value]) -> Result<Value, HostError> + 'static,
    {
        Engine::register_host_fn_typed(self, namespace, name, args, ret, f);
    }
    fn register_host_fn_variadic<F>(&mut self, namespace: &str, name: &str, f: F)
    where
        F: Fn(&[Value]) -> Result<Value, HostError> + 'static,
    {
        Engine::register_host_fn_variadic(self, namespace, name, f);
    }
}

impl Hosts for HostRegistry {
    fn register_host_fn<Marker, F>(&mut self, namespace: &str, name: &str, f: F)
    where
        F: IntoHostFn<Marker>,
    {
        HostRegistry::register_host_fn(self, namespace, name, f);
    }
    fn register_host_fn_typed<F>(&mut self, namespace: &str, name: &str, args: Vec<HostType>, ret: HostType, f: F)
    where
        F: Fn(&[Value]) -> Result<Value, HostError> + 'static,
    {
        HostRegistry::register_host_fn_typed(self, namespace, name, args, ret, f);
    }
    fn register_host_fn_variadic<F>(&mut self, namespace: &str, name: &str, f: F)
    where
        F: Fn(&[Value]) -> Result<Value, HostError> + 'static,
    {
        HostRegistry::register_host_fn_variadic(self, namespace, name, f);
    }
}

/// Every host function of the `drift` module.
fn register_hosts<H: Hosts>(e: &mut H) {
    let v = |args: &[Value], i: usize| args.get(i).cloned().unwrap_or(Value::Null);
    e.register_host_fn("drift", "entity_new", || {
        let e = unsafe { drift_bridge_new() };
        SPAWNED.with(|s| s.borrow_mut().insert(e));
        e as i64
    });
    // Names the game does not have answer the error text, which the module
    // raises: an error a host function raises reaches the script without it.
    e.register_host_fn("drift", "system_error", |name: &str| system_error(name));
    e.register_host_fn("drift", "system_switch", |name: String, on: bool| queue(Command::Switch(name, on)));
    e.register_host_fn("drift", "system_enabled", |name: &str| {
        SWITCHES.with(|w| w.borrow().get(name).copied().unwrap_or(false))
    });
    e.register_host_fn("drift", "restart", || queue(Command::Restart));
    e.register_host_fn("drift", "scene_save", |path: String, root: i64| queue(Command::SaveScene(path, root as u64)));
    e.register_host_fn_variadic("drift", "spawn_bundle", move |a| {
        let id = v(a, 0).as_i64().ok_or_else(|| host_error("spawn needs an entity"))? as u64;
        let bundle = match v(a, 1) {
            Value::Array(items) => items,
            other => vec![other],
        };
        queue(Command::Spawn(id, bundle));
        Ok(Value::Null)
    });
    // A lookup's component of one entity, as [value] or [], noted so the
    // host can tell what the system changed in it.
    e.register_host_fn_variadic("drift", "lookup_get", move |a| {
        let (Some(key), Some(id)) = (v(a, 0).as_i64(), v(a, 1).as_i64()) else { return Ok(Value::Array(Vec::new())) };
        let Some(name) = lookup_name(key) else { return Ok(Value::Array(Vec::new())) };
        let e = id as u64;
        Ok(Value::Array(match read_component(e, &name) {
            Some(row) => {
                // Game components are compared against their store instead.
                if !is_script_component(&name) {
                    LOOKUP_SEEN.with(|seen| seen.borrow_mut().insert((key, e), row.clone()));
                }
                vec![row]
            }
            None => Vec::new(),
        }))
    });
    e.register_host_fn("drift", "lookup_has", |key: i64, id: i64| {
        lookup_name(key).is_some_and(|name| has_component(id as u64, &name))
    });
    e.register_host_fn("drift", "lookup_len", |key: i64| lookup_name(key).map_or(0, |name| entities_with(&name).len() as i64));
    e.register_host_fn_variadic("drift", "insert_component", move |a| {
        let id = v(a, 0).as_i64().unwrap_or(0) as u64;
        queue(Command::Insert(id, v(a, 1)));
        Ok(Value::Null)
    });
    e.register_host_fn("drift", "remove_component", |id: i64, name: String| queue(Command::Remove(id as u64, name)));
    e.register_host_fn("drift", "despawn_entity", |id: i64| queue(Command::Despawn(id as u64)));
    e.register_host_fn_variadic("drift", "insert_resource", move |a| {
        queue(Command::InsertResource(v(a, 0)));
        Ok(Value::Null)
    });
    e.register_host_fn("drift", "remove_resource", |name: &str| queue(Command::RemoveResource(name.to_owned())));
    e.register_host_fn_variadic("drift", "send_message", move |a| {
        queue(Command::Send(v(a, 0)));
        Ok(Value::Null)
    });
    e.register_host_fn_variadic("drift", "send_message_after", move |a| {
        queue(Command::SendAfter(v(a, 0).as_f64().unwrap_or(0.0), v(a, 1)));
        Ok(Value::Null)
    });
    // Values of a type the script names, from JSON text, and back. The
    // answer is [value], or the reason as text when the text is not JSON;
    // the module raises drift_bad_data with it, since a host error reaches
    // the script as host_fn_error.
    e.register_host_fn_variadic("drift", "value_from_json", move |a| {
        let text = v(a, 0);
        let ty_name = v(a, 1);
        let (Some(text), Some(ty_name)) = (text.as_str(), ty_name.as_str()) else {
            return Err(host_error("value_from_json needs text and a type"));
        };
        let j: Json = match serde_json::from_str(text) {
            Ok(j) => j,
            Err(e) => return Ok(Value::String(e.to_string())),
        };
        TYPES.with(|t| {
            let t = t.borrow();
            let Some(types) = t.as_ref() else { return Ok(Value::String("the game is not loaded yet".into())) };
            let Some(ty) = types.parse_type(ty_name) else { return Ok(Value::String(format!("no type {ty_name}"))) };
            Ok(Value::Array(vec![types.from_json(&j, &ty)]))
        })
    });
    e.register_host_fn_variadic("drift", "value_to_json", move |a| Ok(Value::String(to_json(&v(a, 0)).to_string())));

    let b = |j: Json| j.as_bool().unwrap_or(false);
    let f = |j: Json| j.as_f64().unwrap_or(0.0);
    let i = |j: Json| j.as_i64().unwrap_or(0);
    let fv = |j: Json| -> Vec<f64> { j.as_array().map(|a| a.iter().map(|x| x.as_f64().unwrap_or(0.0)).collect()).unwrap_or_default() };
    e.register_host_fn("drift", "key_state", move |k: i64, s: i64| b(call("key_state", json!([k, s]))));
    e.register_host_fn("drift", "mouse_state", move |k: i64, s: i64| b(call("mouse_state", json!([k, s]))));
    e.register_host_fn("drift", "pad_state", move |p: i64, k: i64, s: i64| b(call("pad_state", json!([p, k, s]))));
    e.register_host_fn("drift", "pad_axis", move |p: i64, a: i64| f(call("pad_axis", json!([p, a]))));
    e.register_host_fn("drift", "mouse_vec", move |k: i64| fv(call("mouse_vec", json!([k]))));
    e.register_host_fn("drift", "bind", |n: String, t: String| {
        call("bind", json!([n, t]));
    });
    e.register_host_fn("drift", "bind_axis2d_keys", |n: String, l: i64, r: i64, u: i64, d: i64| {
        call("bind_axis2d_keys", json!([n, l, r, u, d]));
    });
    e.register_host_fn("drift", "bind_stick", |n: String, x: i64, y: i64| {
        call("bind_stick", json!([n, x, y]));
    });
    // The engine answers null for an action the game never declared: the
    // state is then -1, and the values are empty lists.
    e.register_host_fn("drift", "action_state", move |n: String, s: i64| {
        let j = call("action_state", json!([n, s]));
        if j.is_null() { -1 } else { i64::from(b(j)) }
    });
    e.register_host_fn("drift", "action_value", move |n: &str| {
        let j = call("action_value", json!([n]));
        if j.is_null() { Vec::new() } else { vec![f(j)] }
    });
    e.register_host_fn("drift", "action_axis", move |n: &str| {
        let j = call("action_axis", json!([n]));
        if j.is_null() { Vec::new() } else { vec![f(j)] }
    });
    e.register_host_fn("drift", "action_axis2d", move |n: &str| {
        let j = call("action_axis2d", json!([n]));
        if j.is_null() { Vec::new() } else { fv(j) }
    });
    e.register_host_fn("drift", "action_error", |n: &str| action_error(n));
    e.register_host_fn("drift", "texture_load", move |p: &str| -> Result<i64, HostError> {
        let id = i(call("texture_load", json!([p])));
        if id == 0 { Err(host_error(format!("drift_load_failed: {p}"))) } else { Ok(id) }
    });
    e.register_host_fn("drift", "texture_create", move |w: i64, h: i64, px: Vec<f64>| i(call("texture_create", json!([w, h, px]))));
    e.register_host_fn("drift", "texture_size", move |t: i64| fv(call("texture_size", json!([t]))));
    e.register_host_fn("drift", "texture_filter", |t: i64, m: i64| {
        call("texture_filter", json!([t, m]));
    });
    e.register_host_fn("drift", "sound_load", move |p: &str| -> Result<i64, HostError> {
        let id = i(call("sound_load", json!([p])));
        if id == 0 { Err(host_error(format!("drift_load_failed: {p}"))) } else { Ok(id) }
    });
    e.register_host_fn("drift", "sound_create", move |s: Vec<f64>, rate: i64| i(call("sound_create", json!([s, rate]))));
    e.register_host_fn("drift", "quit", || {
        call("quit", json!([]));
    });
    e.register_host_fn("drift", "window_size", move || fv(call("window_size", json!([]))));
    e.register_host_fn("drift", "flash", |r: f64, g: f64, bl: f64, a: f64, s: f64| {
        call("flash", json!([r, g, bl, a, s]));
    });
    e.register_host_fn_variadic("drift", "burst", move |a| {
        call("burst", json!([to_json(&v(a, 0)), to_json(&v(a, 1)), to_json(&v(a, 2))]));
        Ok(Value::Null)
    });
    e.register_host_fn("drift", "save_get_int", move |s: String, k: String, d: i64| i(call("save_get", json!([s, k, d]))));
    e.register_host_fn("drift", "save_set_int", |s: String, k: String, val: i64| {
        call("save_set", json!([s, k, val]));
    });
    e.register_host_fn("drift", "save_get_float", move |s: String, k: String, d: f64| f(call("save_get", json!([s, k, d]))));
    e.register_host_fn("drift", "save_set_float", |s: String, k: String, val: f64| {
        call("save_set", json!([s, k, val]));
    });
    e.register_host_fn("drift", "save_get_string", |s: String, k: String, d: String| {
        call("save_get", json!([s, k, d])).as_str().unwrap_or("").to_owned()
    });
    e.register_host_fn("drift", "save_set_string", |s: String, k: String, val: String| {
        call("save_set", json!([s, k, val]));
    });
    e.register_host_fn("drift", "save_get_bool", move |s: String, k: String, d: bool| b(call("save_get", json!([s, k, d]))));
    e.register_host_fn("drift", "save_set_bool", |s: String, k: String, val: bool| {
        call("save_set", json!([s, k, val]));
    });
    e.register_host_fn("drift", "save_has", move |s: String, k: String| b(call("save_has", json!([s, k]))));
    e.register_host_fn("drift", "save_erase", |s: String, k: String| {
        call("save_erase", json!([s, k]));
    });
    e.register_host_fn("drift", "save_clear", |s: String| {
        call("save_clear", json!([s]));
    });
    e.register_host_fn("drift", "save_flush", |s: String| -> Result<(), HostError> {
        if call("save_flush", json!([s])).as_bool().unwrap_or(false) { Ok(()) } else { Err(host_error("drift_save_failed")) }
    });
    e.register_host_fn("drift", "shake", |c: i64, s: f64| {
        call("shake", json!([c, s]));
    });
    e.register_host_fn("drift", "log_message", |level: i64, m: String| log(level as i32, &m));
    e.register_host_fn("drift", "resource_text", |path: &str| resource_text(path));
    e.register_host_fn("drift", "resource_field_error", |ty: String, path: String| {
        resource_field_check(&ty, &path).err().unwrap_or_default()
    });
    for (name, args, ret) in forwarded().into_iter().chain(presentation_forwarded()) {
        forward(e, name, args, ret);
    }
    for (name, ret) in presentation_values() {
        forward_values(e, name, ret);
    }
}

/// Engine functions the host only passes through: the arguments go to the
/// engine as JSON and its JSON answer comes back as the declared type.
fn forwarded() -> Vec<(&'static str, Vec<HostType>, HostType)> {
    use HostType::{Bool as B, Float as F, Int as I, String as S, Unit as U};
    let ai = || HostType::Array(Box::new(I));
    let af = || HostType::Array(Box::new(F));
    let rng = || vec![I, I, I];
    let with = |mut a: Vec<HostType>, b: Vec<HostType>| {
        a.extend(b);
        a
    };
    let noise = || vec![I, I, F, I, F, F, F, F];
    vec![
        // Random numbers: the generator state crosses as (hi, lo, inc).
        ("rng_seed", vec![I, I], ai()),
        ("rng_int", with(rng(), vec![S, I, I]), ai()),
        ("rng_float", with(rng(), vec![S, F, F]), af()),
        ("rng_weighted", with(rng(), vec![af()]), ai()),
        ("hash_text", vec![S], I),
        ("random_seed", vec![], I),
        ("random_stream", vec![S], ai()),
        // Noise: seed, type, frequency, octaves, lacunarity, gain, warp, warp frequency.
        ("noise_sample", with(noise(), vec![F, F]), F),
        ("noise_grid", with(noise(), vec![F, F, I, I, F]), af()),
        ("noise_cells", with(noise(), vec![I, I, F, I, I]), ai()),
        // Generators.
        ("gen_cave", with(rng(), vec![I, I, F, I, I, I, B, B]), ai()),
        ("gen_dungeon", with(rng(), vec![I, I, I, I, I, I, I, B]), ai()),
        ("gen_walk", with(rng(), vec![I, I, F, I, F, I, I, I]), ai()),
        ("gen_poisson", with(rng(), vec![F, F, F, F, F, I]), af()),
        ("gen_poisson_grid", with(rng(), vec![I, I, ai(), I, F, I]), af()),
        ("gen_wfc", with(rng(), vec![I, I, ai(), I, I, I, B, ai()]), ai()),
        ("grid_op", vec![I, I, ai(), S, I, I, I], ai()),
        // Pathfinding over a grid's cells.
        ("path_find", vec![I, I, ai(), ai(), I, I, I, I, B], ai()),
        ("flow_field", vec![I, I, ai(), ai(), ai(), B], af()),
        // Tilemaps.
        ("map_load", vec![S, S], I),
        ("map_create", vec![I, I, I, I, I, HostType::Array(Box::new(S))], I),
        ("map_info", vec![I], af()),
        ("map_layer", vec![I, S], I),
        ("map_layer_name", vec![I, I], S),
        ("map_layer_visible", vec![I, I, B], U),
        ("map_layer_solid", vec![I, I, B, ai()], U),
        ("map_tile", vec![I, I, I, I, I], I),
        ("map_set_tile", vec![I, I, I, I, I, I], U),
        ("map_tile_at", vec![I, I, F, F], I),
        ("map_world_to_tile", vec![I, F, F, I], ai()),
        ("map_tile_to_world", vec![I, I, I, I], af()),
        ("map_fill", vec![I, I, I, I, ai(), ai(), I, I], U),
        ("map_read", vec![I, I], ai()),
        ("map_property", vec![I, S], S),
        ("map_destroy", vec![I], U),
        // Time and entities.
        ("set_time_scale", vec![F], U),
        ("entity_alive", vec![I], B),
        ("entity_count", vec![], I),
        // Hierarchy and procedural animation.
        ("hier_parent", vec![I], I),
        ("hier_children", vec![I], ai()),
        ("hier_descendants", vec![I], ai()),
        ("hier_world", vec![I], af()),
        ("hier_to_world", vec![I, F, F], af()),
        ("hier_to_local", vec![I, F, F], af()),
        ("anim_spring", vec![I, F, F, F, F, F, F], af()),
        ("anim_spring2", vec![I, F, F, F, F, F, F, F, F, F], af()),
        ("anim_two_bone", vec![F, F, F, F, F, F, F], af()),
        ("anim_fabrik", vec![af(), af(), F, F, I, F, F, af(), af()], af()),
        ("anim_verlet", vec![af(), af(), F, F, F, F, F, F, F, I, F, F, B, F, F], af()),
        ("anim_foot", vec![F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, B], af()),
        // The browser page and the platform.
        ("window_lock_pointer", vec![B], U),
        ("pixel_ratio", vec![], F),
        ("open_url", vec![S], U),
        ("clipboard_set", vec![S], U),
        ("clipboard_get", vec![], S),
        ("page_query", vec![S], S),
        ("local_time", vec![], af()),
        ("is_web", vec![], B),
        ("is_touch", vec![], B),
        // Data files, settings and the player's language.
        ("data_read", vec![S, S], S),
        ("setting_text", vec![S], S),
        ("system_language", vec![], S),
        // HTTP and WebSockets; answers arrive as messages.
        ("http_request", vec![S, S, S, S], I),
        ("ws_open", vec![S], I),
        ("ws_send", vec![I, S], B),
        ("ws_close", vec![I], U),
    ]
    .into_iter()
    .chain(gameplay_forwarded())
    .collect()
}

// ---- Gameplay: input, physics, audio, scenes, app ----

/// The gameplay half's engine functions, passed through like `forwarded`.
fn gameplay_forwarded() -> Vec<(&'static str, Vec<HostType>, HostType)> {
    use HostType::{Bool as B, Float as F, Int as I, String as S, Unit as U};
    let af = || HostType::Array(Box::new(F));
    let as_ = || HostType::Array(Box::new(S));
    vec![
        // Input.
        ("pad_connected", vec![I], B),
        ("bind_axis_keys", vec![S, I, I], U),
        ("input_bindings", vec![S, I], as_()),
        ("input_rebind", vec![S, I, I, S], U),
        ("input_capture", vec![], S),
        ("input_tuning", vec![F, F, I], U),
        ("input_bindings_json", vec![], S),
        ("input_load_bindings", vec![S], I),
        ("input_send", vec![I, I, B, F, F], U),
        ("text_input", vec![I], S),
        ("touch_list", vec![], af()),
        // Physics. Masks pick the collider categories a query sees.
        ("physics_gravity", vec![], af()),
        ("physics_set_gravity", vec![F, F], U),
        ("physics_raycast", vec![F, F, F, F, F, I], af()),
        ("physics_overlap", vec![I, F, F, F, F, I], HostType::Array(Box::new(I))),
        ("physics_push", vec![I, I, F, F], U),
        ("physics_debug", vec![B], U),
        // Shapes of textures' solid pixels.
        ("physics_texture_shapes", vec![I, af(), F, F, F, B], af()),
        ("physics_rebuild_shapes", vec![I, I], U),
        // Tilemap objects and autotiling.
        ("map_objects", vec![I, S], af()),
        ("map_object_names", vec![I, S], as_()),
        ("map_object_property", vec![I, I, I, S], S),
        ("map_tile_property", vec![I, I, S], S),
        ("map_autotile", vec![I, I, I, I, HostType::Array(Box::new(I)), HostType::Array(Box::new(I)), I, B], U),
        // Spatial index and sight on grids.
        ("spatial_query", vec![I, F, F, F, F, I], HostType::Array(Box::new(I))),
        ("grid_fov", vec![I, I, HostType::Array(Box::new(I)), HostType::Array(Box::new(I)), I, I, I], HostType::Array(Box::new(I))),
        ("grid_los", vec![I, I, HostType::Array(Box::new(I)), HostType::Array(Box::new(I)), I, I, I, I], B),
        ("grid_line", vec![I, I, I, I], HostType::Array(Box::new(I))),
        // Steering.
        ("steer", vec![S, af(), af(), af(), HostType::Array(Box::new(I))], af()),
        // Scenes.
        ("scene_load", vec![S], I),
        ("scene_spawn", vec![I, F, F, I, as_(), as_()], HostType::Array(Box::new(I))),
        ("scene_names", vec![], as_()),
        // Audio.
        ("voice_play", vec![I, I, F, F, F, B, F, I], I),
        ("voice_op", vec![I, I, F, F], F),
        ("music_load", vec![S], I),
        ("sound_info", vec![I, I, I], F),
        ("music_op", vec![I, I, F, B], F),
        ("music_playlist", vec![HostType::Array(Box::new(I)), F, B, B, F], U),
        ("audio_bus", vec![S, I], I),
        ("bus_op", vec![I, I, F, F], F),
    ]
}

// ---- Rendering and presentation ----

/// Engine functions for cameras, drawing, effects, assets, the window and
/// UI widgets, passed through like `forwarded()`.
fn presentation_forwarded() -> Vec<(&'static str, Vec<HostType>, HostType)> {
    use HostType::{Float as F, Int as I, String as S, Unit as U};
    let af = || HostType::Array(Box::new(F));
    vec![
        ("camera_point", vec![I, I, F, F], af()),
        ("camera_view", vec![I], af()),
        ("draw_shape", vec![S, I, F, af(), af()], U),
        ("draw_nine", vec![I, af(), af(), I, F], U),
        ("draw_clip", vec![af()], U),
        ("particles_restart", vec![I], U),
        // Images: pixel buffers the engine holds, by id.
        ("image_new", vec![I, I, af()], I),
        ("image_load", vec![S], I),
        ("image_from_texture", vec![I], I),
        ("image_info", vec![I], af()),
        ("image_get", vec![I, I, I], af()),
        ("image_set", vec![I, I, I, af()], U),
        ("image_op", vec![I, S, af(), af(), af()], U),
        ("image_blit", vec![I, I, I, I, af(), HostType::Bool], U),
        ("image_crop", vec![I, af()], I),
        ("image_read", vec![I, af()], HostType::Array(Box::new(I))),
        ("image_write", vec![I, af(), HostType::Array(Box::new(I))], U),
        ("image_texture", vec![I], I),
        ("image_save", vec![I, S], HostType::Bool),
        ("image_free", vec![I], U),
        ("texture_update", vec![I, I, af()], U),
        ("download", vec![S], U),
        ("window_screenshot", vec![S], U),
        ("render_target", vec![I, I], I),
        // Post effects, lighting and tweens.
        ("effect_op", vec![I, S, F], F),
        ("effect_material", vec![I, I], I),
        ("fade_to", vec![F, F, F, F, F, F], U),
        ("lighting", vec![S, af()], U),
        ("tween_speed", vec![F], U),
        // The window, fonts, shaders and materials.
        ("window_op", vec![S, af(), S], af()),
        ("window_backend", vec![], S),
        ("font_load", vec![S, F], I),
        ("font_line_height", vec![I, F], F),
        ("font_fallback", vec![I, I], U),
        ("shader_load", vec![S], I),
        ("shader_create", vec![S, S], I),
        ("material_create", vec![I], I),
        ("material_param", vec![I, I, F, F, F, F], U),
        ("material_texture", vec![I, I, I], U),
        // Sprite sheets: grids and Aseprite files.
        ("sheet_grid", vec![I, F, F], af()),
        ("sheet_aseprite", vec![S], af()),
        ("sheet_tag", vec![I, S], af()),
        ("sheet_tags", vec![I], S),
    ]
}

/// Engine functions that take whole values (a Text style, a Sprite, the
/// settings of a post effect): each argument goes as JSON, and a struct
/// carries its type's name in `__type`.
fn presentation_values() -> Vec<(&'static str, HostType)> {
    vec![
        ("draw_text", HostType::Unit),
        ("draw_sprite", HostType::Unit),
        ("effect_add", HostType::Int),
        ("effect_set", HostType::Unit),
        ("text_measure", HostType::Array(Box::new(HostType::Float))),
    ]
}

fn typed_json(v: &Value) -> Json {
    let mut j = to_json(v);
    if let (Value::Struct { name, .. }, Json::Object(o)) = (v, &mut j) {
        o.insert("__type".into(), json!(bare(name)));
    }
    j
}

fn forward_values<H: Hosts>(e: &mut H, name: &'static str, ret: HostType) {
    e.register_host_fn_variadic("drift", name, move |a| {
        let answer = call(name, Json::Array(a.iter().map(typed_json).collect()));
        Ok(host_value(&answer, &ret))
    });
}

fn forward<H: Hosts>(e: &mut H, name: &'static str, args: Vec<HostType>, ret: HostType) {
    let shape = ret.clone();
    let f = cstr(name);
    e.register_host_fn_typed("drift", name, args, ret, move |a| {
        let mut bytes = Vec::new();
        wire::write_values(&mut bytes, a);
        let mut len = 0;
        let p = unsafe { drift_bridge_call(f.as_ptr(), bytes.as_ptr(), bytes.len(), &mut len) };
        if p.is_null() {
            return Ok(host_value(&Json::Null, &shape));
        }
        let answer = unsafe { std::slice::from_raw_parts(p, len) };
        Ok(host_value_wire(wire::Item::new(answer), &shape))
    });
}

/// host_value, read straight from the engine's bytes.
fn host_value_wire(item: wire::Item<'_>, t: &HostType) -> Value {
    use wire::Kind;
    match (t, item.kind()) {
        (HostType::Int, Kind::Int(i)) => Value::Int(i),
        (HostType::Int, Kind::Float(f)) => Value::Int(f as i64),
        (HostType::Int, _) => Value::Int(0),
        (HostType::Float, Kind::Int(i)) => Value::Float(i as f64),
        (HostType::Float, Kind::Float(f)) => Value::Float(f),
        (HostType::Float, _) => Value::Float(0.0),
        (HostType::Bool, Kind::Bool(b)) => Value::Bool(b),
        (HostType::Bool, _) => Value::Bool(false),
        (HostType::String, Kind::Str(s)) => Value::String(s.to_owned()),
        (HostType::String, _) => Value::String(String::new()),
        (HostType::Array(elem), _) => Value::Array(item.elements().map(|x| host_value_wire(x, elem)).collect()),
        _ => Value::Null,
    }
}

fn host_value(j: &Json, t: &HostType) -> Value {
    match t {
        HostType::Int => Value::Int(j.as_i64().or_else(|| j.as_f64().map(|f| f as i64)).unwrap_or(0)),
        HostType::Float => Value::Float(j.as_f64().unwrap_or(0.0)),
        HostType::Bool => Value::Bool(j.as_bool().unwrap_or(false)),
        HostType::String => Value::String(j.as_str().unwrap_or("").to_owned()),
        HostType::Array(elem) => {
            Value::Array(j.as_array().map(|a| a.iter().map(|x| host_value(x, elem)).collect()).unwrap_or_default())
        }
        _ => Value::Null,
    }
}

/// Compiles the game and builds its systems and resources.
/// Something wrong with a game, found while reading it, and the item it is
/// about. The message starts with its code, `drift_...: `.
#[derive(Clone, Debug)]
struct Finding {
    item: Item,
    message: String,
}

#[derive(Clone, Debug)]
enum Item {
    /// A function of the game, by name: a system or a run_if condition.
    Fn(String),
    /// A struct of the game, by name.
    Struct(String),
    /// A drift.toml key, as `section.key`, or a section.
    Setting(String),
}

/// What a load reads before it builds anything or runs any of the game:
/// the program, its attributes, its systems and conditions in order, and
/// which resources can be built from their defaults. `drift check` reports
/// `errors` and `warnings`; a load refuses the game on an error and logs a
/// warning.
struct Reading {
    host: Host,
    resources_list: Vec<(u16, Option<String>)>,
    buildable: Vec<(u16, Option<String>)>,
    /// The function that builds each buildable struct from its defaults.
    default_fn: HashMap<u16, String>,
    message_ids: HashSet<u16>,
    errors: Vec<Finding>,
    warnings: Vec<Finding>,
}

/// Why a game could not be read.
enum Unread {
    /// The entry cannot be read: the reason.
    NoEntry(String),
    /// The game does not compile: the compiler's error and the entry's text.
    Compile(Diagnostic, String),
}

/// Compiles the game and reads it.
fn read_game(entry: &str, module_dir: &str) -> Result<Reading, Unread> {
    LOOKUP_NAMES.with(|l| l.borrow_mut().clear());
    let src = std::fs::read_to_string(entry).map_err(|e| Unread::NoEntry(format!("{entry}: {e}")))?;
    let main = if src.contains("fn main(") { String::new() } else { String::from("\nfn main() {}\n") };

    // The game is compiled with a function that builds each resource,
    // component and message from its defaults; a resource with a field that
    // has no default cannot be built and waits for cmd.insert_resource. Which
    // structs those are, the attributes say once the game has compiled, so
    // the first compile takes them from a reading of the game's files; when
    // that reading missed one, the game compiles again with the list the
    // attributes give.
    let base = format!("{src}{main}");
    // The glue for `names`, and where each function sits in the compiled text.
    let glue = |names: &[(String, String)]| -> (String, Vec<std::ops::Range<usize>>) {
        let mut g = String::new();
        let mut at = Vec::with_capacity(names.len());
        for (f, n) in names {
            let start = base.len() + g.len();
            g.push_str(&format!("\nfn {f}() -> {n} {{ return {n} {{ .. }}; }}\n"));
            at.push(start..base.len() + g.len());
        }
        (g, at)
    };
    // Compiles with the glue for `names`, dropping each name whose function
    // does not compile (found from where the error points), with the error,
    // which costs one more compile per such struct. An error elsewhere is
    // the game's.
    let compile_with = |names: &mut Vec<(String, String)>, dropped: &mut Vec<(String, Diagnostic)>| -> Result<Program, Diagnostic> {
        loop {
            let (g, at) = glue(names);
            match engine(module_dir).compile(&format!("{base}{g}"), entry) {
                Ok(p) => return Ok(p),
                Err(d) => {
                    let ours = d.filename == entry || d.filename.is_empty();
                    match at.iter().position(|r| ours && r.contains(&d.span.start)) {
                        Some(i) => dropped.push((names.remove(i).1, d)),
                        None => return Err(d),
                    }
                }
            }
        }
    };
    // A page `drift build --web` wrote carries the game compiled to
    // bytecode, and starts without compiling (see `built_program`).
    let built = if USE_BUILT.with(std::cell::Cell::get) { built_program(entry) } else { None };
    let mut guessed: Vec<(String, String)> = Vec::new();
    let mut dropped: Vec<(String, Diagnostic)> = Vec::new();
    let first = if built.is_some() {
        None
    } else {
        guessed = marked_structs(entry, module_dir).into_iter().enumerate().map(|(i, n)| (format!("__drift_default_g{i}"), n)).collect();
        Some(match compile_with(&mut guessed, &mut dropped) {
            Ok(p) => p,
            // The glue may be what failed (a default that raises somewhere
            // the error cannot place): the game alone says whose error it is.
            Err(_) => {
                guessed.clear();
                dropped.clear();
                engine(module_dir).compile(&base, entry).map_err(|d| Unread::Compile(d, src.clone()))?
            }
        })
    };
    let table = match (&built, &first) {
        (Some(b), _) => b.program.attributes(),
        (None, Some(p)) => p.attributes(),
        (None, None) => unreachable!("a game is built or compiled"),
    };
    let types = std::rc::Rc::new(Types::new(table));
    let named = |n: &str| -> Vec<(u16, Option<String>)> {
        types
            .table
            .named("drift", n)
            .filter_map(|a| match a.target {
                AttributeTarget::Struct(id) => Some((id, a.arg("section").and_then(|s| match s {
                    AttributeValue::String(s) => Some(s.clone()),
                    _ => None,
                }))),
                _ => None,
            })
            .collect()
    };
    let components: HashSet<String> = named("component").iter().map(|(id, _)| types.struct_name(*id).to_owned()).collect();
    let messages_kinds: HashSet<String> = named("message").iter().map(|(id, _)| types.struct_name(*id).to_owned()).collect();
    let mut resources_list: Vec<(u16, Option<String>)> = named("resource");
    let settings = named("settings");
    resources_list.extend(settings.iter().cloned());
    let resource_kinds: HashSet<String> = resources_list.iter().map(|(id, _)| types.struct_name(*id).to_owned()).collect();

    // Components too, so a component another one requires can be added from
    // its defaults, and messages, whose new fields take their defaults when
    // a reload carries a waiting message over.
    let component_list: Vec<(u16, Option<String>)> = named("component").iter().map(|(id, _)| (*id, None)).collect();
    let message_list: Vec<(u16, Option<String>)> = named("message").iter().map(|(id, _)| (*id, None)).collect();
    let message_ids: HashSet<u16> = message_list.iter().map(|m| m.0).collect();
    let all_defaults: Vec<(u16, Option<String>)> =
        resources_list.iter().cloned().chain(component_list.iter().cloned()).chain(message_list.iter().cloned()).collect();

    // The default function of each struct the first compile built, and
    // whether it covered them all: a struct the reading found whose function
    // did not compile has no defaults to build, and the error says why.
    let mut default_fn: HashMap<u16, String> = HashMap::new();
    let mut unbuildable: Vec<(u16, Diagnostic)> = Vec::new();
    let mut covered = true;
    for (id, _) in &all_defaults {
        let n = types.struct_name(*id);
        if let Some((f, _)) = guessed.iter().find(|(_, g)| g == n) {
            default_fn.insert(*id, f.clone());
        } else if let Some((_, d)) = dropped.iter().find(|(g, _)| g == n) {
            unbuildable.push((*id, d.clone()));
        } else {
            covered = false;
        }
    }
    let program = if let Some(b) = built {
        for (id, _) in &all_defaults {
            if let Some(f) = b.defaults.get(types.struct_name(*id)) {
                default_fn.insert(*id, f.clone());
            }
        }
        unbuildable.clear();
        Prog::Built(b.program, b.calls)
    } else if covered {
        Prog::Source(first.expect("compiled"))
    } else {
        default_fn.clear();
        unbuildable.clear();
        let mut names: Vec<(String, String)> =
            all_defaults.iter().map(|(id, _)| (format!("__drift_default_{id}"), types.struct_name(*id).to_owned())).collect();
        let mut failed = Vec::new();
        let program = match compile_with(&mut names, &mut failed) {
            Ok(p) => p,
            // An error the glue cannot place: build no defaults rather than
            // stop a game that compiles without them.
            Err(d) => {
                names.clear();
                engine(module_dir).compile(&base, entry).map_err(|_| Unread::Compile(d, src.clone()))?
            }
        };
        for (id, _) in &all_defaults {
            let f = format!("__drift_default_{id}");
            let n = types.struct_name(*id);
            if names.iter().any(|(g, _)| *g == f) {
                default_fn.insert(*id, f);
            } else if let Some((_, d)) = failed.iter().find(|(g, _)| g == n) {
                unbuildable.push((*id, d.clone()));
            }
        }
        Prog::Source(program)
    };
    let buildable: Vec<(u16, Option<String>)> = all_defaults.iter().filter(|(id, _)| default_fn.contains_key(id)).cloned().collect();

    let mut host = Host {
        program,
        types,
        systems: Vec::new(),
        components,
        messages_kinds,
        resource_kinds,
        resources: HashMap::new(),
        resources_version: 0,
        messages: Vec::new(),
        later: Vec::new(),
        next_seq: 0,
        contacts: Vec::new(),
        started: false,
        initial_resources: HashMap::new(),
        initial_enabled: Vec::new(),
        restart_pending: false,
        failed: HashSet::new(),
        pending_startup: Vec::new(),
        pending_enter: Vec::new(),
        reported: HashSet::new(),
        conditions: HashMap::new(),
        condition_cache: HashMap::new(),
        requires: HashMap::new(),
        required: HashMap::new(),
        component_defaults: HashMap::new(),
        message_defaults: HashMap::new(),
        states: HashMap::new(),
        time_scale: (1.0, 0.0),
    };
    // Resource defaults may load data files, which needs the types, and a
    // system may take a state, which needs the states.
    TYPES.with(|t| *t.borrow_mut() = Some(host.types.clone()));
    for a in host.types.table.named("drift", "state") {
        let AttributeTarget::Enum(id) = a.target else { continue };
        let info = &host.types.table.enums[id as usize];
        if let Some(first) = info.variants.first() {
            host.states.insert(bare(&info.name).to_owned(), StateNow { now: first.clone(), since: 0.0, next: None });
        }
    }
    let mut errors: Vec<Finding> = Vec::new();
    let mut warnings: Vec<Finding> = Vec::new();

    // A resource that cannot be built from its defaults: settings always
    // should be, and a resource the game's own scope cannot name is one it
    // did not mean to leave out.
    for (id, why) in &unbuildable {
        let name = host.types.struct_name(*id).to_owned();
        if let Some((_, section)) = settings.iter().find(|(s, _)| s == id) {
            warnings.push(Finding {
                item: Item::Struct(name.clone()),
                message: if why.code.starts_with("unknown") {
                    format!(
                        "drift_settings_without_defaults: {name} [{}] is not in the entry's scope ({}), so it is never built and its section is never read. Import it by name, as import \"file.cdl\" {{ {} }};, not only through a namespace",
                        section.clone().unwrap_or_default(),
                        why.message,
                        bare(&name)
                    )
                } else {
                    format!(
                        "drift_settings_without_defaults: {name} [{}] cannot be built from its defaults, so its section is never read: {}",
                        section.clone().unwrap_or_default(),
                        why.message
                    )
                },
            });
        } else if resources_list.iter().any(|(r, _)| r == id) && why.code.starts_with("unknown") {
            warnings.push(Finding {
                item: Item::Struct(name.clone()),
                message: format!(
                    "drift_resource_not_in_scope: {name} is a @resource the entry's scope cannot name ({}), so it is never built from its defaults and systems that take it do not run until cmd.insert_resource adds it. Import it by name, as import \"file.cdl\" {{ {} }};",
                    why.message,
                    bare(&name)
                ),
            });
        }
    }

    // What drift.toml sets for each @settings struct that is built.
    for (id, section) in &settings {
        let Some(section) = section else { continue };
        if !buildable.iter().any(|r| r.0 == *id) {
            continue;
        }
        let j = call("settings_section", json!([section]));
        for (key, why) in host.types.section_problems(&j, *id, section) {
            warnings.push(Finding { item: Item::Setting(key), message: why });
        }
    }

    for a in host.types.table.named("drift", "component") {
        let AttributeTarget::Struct(id) = a.target else { continue };
        let required = type_list(&host.types, a.arg("requires"));
        if !required.is_empty() {
            host.requires.insert(host.types.struct_name(id).to_owned(), required);
        }
    }

    let mut systems = Vec::new();
    for a in host.types.table.named("drift", "system") {
        let AttributeTarget::Fn { name, params } = &a.target else { continue };
        let mut fail = |message: String| errors.push(Finding { item: Item::Fn(name.clone()), message });
        if let Some(why) = host.loose_components(name, params) {
            fail(why);
            continue;
        }
        let mut ps = Vec::new();
        for (pname, ty) in params {
            match host.classify(ty) {
                Ok(p) => ps.push(p),
                Err(why) => fail(format!("drift_unknown_parameter: {name}({pname}): {why}")),
            }
        }
        let rows: Vec<&String> = params
            .iter()
            .zip(&ps)
            .filter(|(_, p)| matches!(p, Param::Row { .. }))
            .map(|((pname, _), _)| pname)
            .collect();
        let events: Vec<&String> = params
            .iter()
            .zip(&ps)
            .filter(|(_, p)| matches!(p, Param::OneContact { .. } | Param::OneMessage(_)))
            .map(|((pname, _), _)| pname)
            .collect();
        if rows.len() > 1 {
            fail(format!(
                "drift_two_rows: {name} takes the rows {} and {}. A system runs once per entity for one row; read other entities with a query such as (Entity, Enemy)[]",
                rows[0], rows[1]
            ));
        } else if !rows.is_empty() && !events.is_empty() {
            fail(format!(
                "drift_row_and_event: {name} takes the row {} and the event {}. A system runs once per entity or once per event, not both; take the events as a list, such as Contact<A, B>[], or read the entity with a Lookup",
                rows[0], events[0]
            ));
        } else if events.len() > 1 {
            fail(format!(
                "drift_two_events: {name} takes the events {} and {}. A system runs once per event for one of them; take the other as a list, such as Overlap<A, B>[]",
                events[0], events[1]
            ));
        }
        let startup = matches!(a.arg("schedule"), Some(AttributeValue::Variant { name: v, .. }) if v == "Startup");
        let enabled = !matches!(a.arg("enabled"), Some(AttributeValue::Bool(false)));
        systems.push(System {
            name: name.clone(),
            params: ps,
            run_if: fn_list(a.arg("run_if")),
            in_state: a
                .arg("in_state")
                .and_then(AttributeValue::as_list)
                .unwrap_or(&[])
                .iter()
                .filter_map(|v| variant(&host.types.table, v))
                .map(|(e, v)| (bare(&e).to_owned(), v))
                .collect(),
            on_enter: a.arg("on_enter").and_then(|v| variant(&host.types.table, v)).map(|(e, v)| (bare(&e).to_owned(), v)),
            on_exit: a.arg("on_exit").and_then(|v| variant(&host.types.table, v)).map(|(e, v)| (bare(&e).to_owned(), v)),
            startup,
            enabled,
            after: fn_list(a.arg("after")),
            before: fn_list(a.arg("before")),
            seen: 0,
        });
    }
    // run_if conditions take parameters the way systems do.
    for sys in &systems {
        for cond in &sys.run_if {
            if host.conditions.contains_key(cond) {
                continue;
            }
            let Some(params) = function_params(&host.types.table, cond) else {
                errors.push(Finding {
                    item: Item::Fn(sys.name.clone()),
                    message: format!("drift_unknown_condition: {cond} in run_if of {}", sys.name),
                });
                continue;
            };
            let mut ps = Vec::new();
            for (pname, ty) in &params {
                let mut fail = |message: String| errors.push(Finding { item: Item::Fn(cond.clone()), message });
                match host.classify(ty) {
                    Ok(p @ (Param::Resource(_) | Param::Time(_) | Param::Engine(_) | Param::Rows { .. } | Param::Lookup { .. })) => ps.push(p),
                    Ok(_) => fail(format!("drift_condition_parameter: {cond}({pname}): conditions read resources and queries")),
                    Err(why) => fail(format!("drift_unknown_parameter: {cond}({pname}): {why}")),
                }
            }
            host.conditions.insert(cond.clone(), System {
                name: cond.clone(),
                params: ps,
                run_if: Vec::new(),
                in_state: Vec::new(),
                on_enter: None,
                on_exit: None,
                startup: false,
                enabled: true,
                after: Vec::new(),
                before: Vec::new(),
                seen: 0,
            });
        }
    }
    match order(systems) {
        Ok(ordered) => host.systems = ordered,
        Err(why) => errors.push(Finding { item: Item::Fn(why.split(": ").nth(1).unwrap_or("").split(", ").next().unwrap_or("").to_owned()), message: why }),
    }
    Ok(Reading { host, resources_list, buildable, default_fn, message_ids, errors, warnings })
}

fn load(entry: &str, module_dir: &str) -> Result<Host, String> {
    let reading = read_game(entry, module_dir).map_err(|why| match why {
        Unread::NoEntry(why) => why,
        Unread::Compile(d, src) => report(&d, entry, &src),
    })?;
    if !reading.errors.is_empty() {
        return Err(reading.errors.iter().map(|f| f.message.as_str()).collect::<Vec<_>>().join("\n"));
    }
    for w in &reading.warnings {
        log(2, &w.message);
    }
    let Reading { mut host, resources_list, buildable, default_fn, message_ids, .. } = reading;
    STORES.with(|s| *s.borrow_mut() = host.components.iter().map(|c| (c.clone(), BTreeMap::new())).collect());
    let resource_ids: HashSet<u16> = resources_list.iter().map(|r| r.0).collect();
    for (id, section) in buildable.iter().map(|r| (r.0, r.1.clone())) {
        let name = host.types.struct_name(id).to_owned();
        let mut v = host
            .program
            .call(&default_fn[&id], &[])
            .map_err(|d| format!("{name}: building its defaults raised {}: {}", d.code, d.message))?;
        if message_ids.contains(&id) {
            host.message_defaults.insert(name, v);
            continue;
        }
        if !resource_ids.contains(&id) {
            host.component_defaults.insert(name, v);
            continue;
        }
        if let Some(section) = section {
            let j = call("settings_section", json!([section]));
            v = host.types.merge(&v, &j, id);
        }
        publish_resource(&name, Some(&v));
        host.resources.insert(name, v);
    }
    host.initial_resources = host.resources.clone();
    COMPONENT_NAMES.with(|n| *n.borrow_mut() = host.components.iter().map(|c| bare(c).to_owned()).collect());
    host.initial_enabled = host.systems.iter().map(|s| s.enabled).collect();
    SWITCHES.with(|w| *w.borrow_mut() = host.systems.iter().map(|s| (s.name.clone(), s.enabled)).collect());
    Ok(host)
}

/// The structs the game's files mark @component, @resource, @settings or
/// @message (with or without `drift::`), by name, read from the files in
/// the entry's folder without compiling them, and those of the `drift`
/// modules the game imports by glob (`import "drift" { * }`), whose names it
/// then uses bare. A struct this misses, or a name that is not the struct's
/// name in the entry, costs a second compile.
fn marked_structs(entry: &str, module_dir: &str) -> Vec<String> {
    let folder = std::path::Path::new(entry).parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(std::path::Path::new("."));
    let mut files = Vec::new();
    let mut dirs = vec![folder.to_path_buf()];
    while let Some(dir) = dirs.pop() {
        let Ok(read) = std::fs::read_dir(&dir) else { continue };
        for item in read.flatten() {
            let path = item.path();
            let name = item.file_name().to_string_lossy().into_owned();
            if path.is_dir() {
                let skip = name.starts_with('.')
                    || name == "build"
                    || name.starts_with("build-")
                    || (cfg!(target_os = "emscripten") && ["dev", "proc", "tmp", "home", "drift-module"].contains(&name.as_str()));
                if !skip {
                    dirs.push(path);
                }
            } else if path.extension().is_some_and(|e| e == "cdl") {
                files.push(path);
            }
        }
    }
    files.sort();
    let module = std::path::Path::new(module_dir).join("src");
    let mut modules = Vec::new();
    for f in &files {
        let Ok(text) = std::fs::read_to_string(f) else { continue };
        for (i, _) in text.match_indices("import \"drift") {
            let rest = &text[i + "import \"drift".len()..];
            let Some(close) = rest.find('"') else { continue };
            let sub = &rest[..close];
            let glob = rest[close..].split(';').next().is_some_and(|tail| tail.contains('*'));
            if !glob {
                continue;
            }
            if sub.is_empty() {
                modules.push(module.join("main.cdl"));
                if let Ok(core) = std::fs::read_dir(module.join("core")) {
                    let mut core: Vec<_> = core.flatten().map(|e| e.path()).filter(|p| p.extension().is_some_and(|e| e == "cdl")).collect();
                    core.sort();
                    modules.extend(core);
                }
            } else if let Some(name) = sub.strip_prefix('/') {
                modules.push(module.join(format!("{name}.cdl")));
            }
        }
    }
    for m in modules {
        if !files.contains(&m) {
            files.push(m);
        }
    }
    let mut names = Vec::new();
    for f in files {
        let Ok(text) = std::fs::read_to_string(&f) else { continue };
        for n in marked_in(&text) {
            if !names.contains(&n) {
                names.push(n);
            }
        }
    }
    names
}

/// The names of the structs `src` marks with a drift attribute that has
/// defaults to build: `@component`, `@resource`, `@settings`, `@message`.
fn marked_in(src: &str) -> Vec<String> {
    let b = src.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    let mut marked = false;
    let ident = |i: usize| -> (String, usize) {
        let mut j = i;
        while j < b.len() && (b[j].is_ascii_alphanumeric() || b[j] == b'_' || b[j] == b':') {
            j += 1;
        }
        (src[i..j].to_owned(), j)
    };
    while i < b.len() {
        let c = b[i];
        if c == b'/' && b.get(i + 1) == Some(&b'/') {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
        } else if c == b'/' && b.get(i + 1) == Some(&b'*') {
            i += 2;
            while i + 1 < b.len() && !(b[i] == b'*' && b[i + 1] == b'/') {
                i += 1;
            }
            i += 2;
        } else if c == b'"' {
            i += 1;
            while i < b.len() && b[i] != b'"' {
                i += if b[i] == b'\\' { 2 } else { 1 };
            }
            i += 1;
        } else if c == b'@' {
            let (name, j) = ident(i + 1);
            let attr = name.rsplit("::").next().unwrap_or("");
            if ["component", "resource", "settings", "message"].contains(&attr) {
                marked = true;
            }
            i = j;
            // The attribute's arguments.
            let mut k = i;
            while k < b.len() && b[k].is_ascii_whitespace() {
                k += 1;
            }
            if b.get(k) == Some(&b'(') {
                let mut depth = 0;
                while k < b.len() {
                    match b[k] {
                        b'(' => depth += 1,
                        b')' => {
                            depth -= 1;
                            if depth == 0 {
                                k += 1;
                                break;
                            }
                        }
                        _ => {}
                    }
                    k += 1;
                }
                i = k;
            }
        } else if c.is_ascii_alphabetic() || c == b'_' {
            let (word, j) = ident(i);
            i = j;
            match word.as_str() {
                "export" | "pub" => {}
                "struct" if marked => {
                    while i < b.len() && b[i].is_ascii_whitespace() {
                        i += 1;
                    }
                    let (name, j) = ident(i);
                    i = j;
                    if !name.is_empty() {
                        out.push(name);
                    }
                    marked = false;
                }
                _ => marked = false,
            }
        } else {
            i += 1;
        }
    }
    out
}

/// A game compiled to bytecode by `drift build --web`, loaded.
struct Built {
    program: RuntimeProgram,
    /// The export each system and condition is called through, by the
    /// function's name.
    calls: HashMap<String, String>,
    /// The export that builds each struct from its defaults, by the
    /// struct's name.
    defaults: HashMap<String, String>,
}

/// The bytecode `drift build --web` writes beside the entry script, `main.cdlb`
/// for `main.cdl`, when it is there and loads. A web page carries its
/// scripts too, so a page whose bytecode does not load (an older format)
/// compiles them instead.
fn built_program(entry: &str) -> Option<Built> {
    let path = format!("{entry}b");
    let bytes = std::fs::read(&path).ok()?;
    let mut program = match candela::load_program(&bytes, &hosts()) {
        Ok(p) => p,
        Err(e) => {
            log(1, &format!("Candela: {path} does not load ({e}); compiling the scripts instead"));
            return None;
        }
    };
    program.run();
    let list = |program: &mut RuntimeProgram, f: &str| -> Option<Vec<String>> {
        match program.call(f, &[]) {
            Ok(Value::Array(items)) => Some(items.iter().filter_map(|v| v.as_str().map(str::to_owned)).collect()),
            _ => None,
        }
    };
    let (Some(calls), Some(defaults)) = (list(&mut program, "__drift_calls"), list(&mut program, "__drift_defaults")) else {
        log(1, &format!("Candela: {path} was not written by drift build; compiling the scripts instead"));
        return None;
    };
    let calls: HashMap<String, String> = calls.into_iter().enumerate().map(|(k, n)| (n, format!("__drift_call_{k}"))).collect();
    let defaults: HashMap<String, String> = defaults.into_iter().enumerate().map(|(k, n)| (n, format!("__drift_default_b{k}"))).collect();
    let exported: HashSet<&str> = program.exports().collect();
    if let Some(missing) = calls.values().chain(defaults.values()).find(|f| !exported.contains(f.as_str())) {
        log(1, &format!("Candela: {path} does not export {missing}; compiling the scripts instead"));
        return None;
    }
    Some(Built { program, calls, defaults })
}

/// The registry a compiled program binds the drift module's host functions from.
fn hosts() -> HostRegistry {
    let mut r = HostRegistry::new();
    register_hosts(&mut r);
    r
}

/// Compiles a game to bytecode a page starts from without compiling: the
/// scripts with a function that builds each struct with defaults, and a
/// function each system and condition is called through, so every one of
/// them is a function the program exports wherever the game declares it.
fn build(entry: &str, module_dir: &str) -> Result<Vec<u8>, String> {
    let reading = read_game(entry, module_dir).map_err(|why| match why {
        Unread::NoEntry(why) => why,
        Unread::Compile(d, src) => report(&d, entry, &src),
    })?;
    if !reading.errors.is_empty() {
        return Err(reading.errors.iter().map(|f| f.message.as_str()).collect::<Vec<_>>().join("\n"));
    }
    let types = reading.host.types.clone();
    let src = std::fs::read_to_string(entry).map_err(|e| format!("{entry}: {e}"))?;
    let main = if src.contains("fn main(") { String::new() } else { String::from("\nfn main() {}\n") };
    let quote = |n: &str| format!("\"{}\"", n.replace('\\', "\\\\").replace('"', "\\\""));
    // A name of the entry that an import also has is qualified by the
    // entry's module; inside the entry the bare name is the entry's own.
    let stem = std::path::Path::new(entry).file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let entry_module = format!("{stem}::");
    let mut glue = String::new();
    let mut defaults = Vec::new();
    for (k, (id, _)) in reading.buildable.iter().enumerate() {
        let name = types.struct_name(*id);
        let n = name.replace(&entry_module, "");
        glue.push_str(&format!("\nfn __drift_default_b{k}() -> {n} {{ return {n} {{ .. }}; }}\n"));
        defaults.push(quote(name));
    }
    let mut calls = Vec::new();
    let mut wrap = |name: &str, returns: bool| -> Result<(), String> {
        let params = function_params(&types.table, name).ok_or_else(|| format!("{name}: no such function"))?;
        let k = calls.len();
        let decl: Vec<String> = params.iter().enumerate().map(|(i, (_, t))| format!("a{i}: {}", types.table.type_name(t).replace(&entry_module, ""))).collect();
        let pass: Vec<String> = (0..params.len()).map(|i| format!("a{i}")).collect();
        let callee = name.strip_prefix(&entry_module).unwrap_or(name);
        if returns {
            glue.push_str(&format!("\nfn __drift_call_{k}({}) -> bool {{ return {callee}({}); }}\n", decl.join(", "), pass.join(", ")));
        } else {
            glue.push_str(&format!("\nfn __drift_call_{k}({}) {{ {callee}({}); }}\n", decl.join(", "), pass.join(", ")));
        }
        calls.push(quote(name));
        Ok(())
    };
    for sys in &reading.host.systems {
        wrap(&sys.name, false)?;
    }
    let mut conditions: Vec<&String> = reading.host.conditions.keys().collect();
    conditions.sort();
    for cond in conditions {
        wrap(cond, true)?;
    }
    glue.push_str(&format!("\nfn __drift_calls() -> string[] {{ return [{}]; }}\n", calls.join(", ")));
    glue.push_str(&format!("\nfn __drift_defaults() -> string[] {{ return [{}]; }}\n", defaults.join(", ")));
    let source = format!("{src}{main}{glue}");
    // Compiled the way a load compiles, so what does not compile says why.
    engine(module_dir).compile(&source, entry).map_err(|d| report(&d, entry, &src))?;
    let mut resolver = candela::compiler::imports::ImportResolver::new();
    resolver.add_package("drift", std::path::PathBuf::from(module_dir));
    if let Some(dir) = lib_dir() {
        resolver.set_lib_dir(dir);
    }
    let options = candela::BuildOptions { release: true, native: candela::NativeCode::None };
    let bytes = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| candela::build_artifact(source, entry, &resolver, &options)))
        .map_err(|_| String::from("the candela compiler stopped unexpectedly"))??;
    // Bytecode exports only functions whose parameters it can take from a
    // host; a game with a system that takes something else (with the pinned
    // candela, a Text, whose align is an enum) keeps compiling when its page
    // loads.
    let program = candela::load_program(&bytes, &hosts()).map_err(|e| e.to_string())?;
    let exported: HashSet<&str> = program.exports().collect();
    let names: Vec<String> = calls.iter().map(|q| q.trim_matches('"').to_owned()).collect();
    for (k, name) in names.iter().enumerate() {
        if !exported.contains(format!("__drift_call_{k}").as_str()) {
            return Err(format!("{name} cannot be called from bytecode: a parameter holds a value its exports do not take, such as an enum"));
        }
    }
    Ok(bytes)
}

/// Compiles the game at `entry` to bytecode (see `build`). Returns the bytes,
/// valid until the next call, or null with drift_host_error saying why.
#[no_mangle]
pub extern "C" fn drift_host_build(entry: *const c_char, module_dir: *const c_char, len: *mut usize) -> *const u8 {
    let (Some(entry), Some(dir)) = (from_c(entry), from_c(module_dir)) else { return std::ptr::null() };
    match std::panic::catch_unwind(|| build(&entry, &dir)) {
        Ok(Ok(bytes)) => BUILT.with(|b| {
            let mut b = b.borrow_mut();
            *b = bytes;
            if !len.is_null() {
                unsafe { *len = b.len() };
            }
            b.as_ptr()
        }),
        Ok(Err(e)) => {
            set_error(&e);
            std::ptr::null()
        }
        Err(_) => {
            set_error("the candela compiler stopped unexpectedly");
            std::ptr::null()
        }
    }
}

/// The parameters of a function the attribute table describes: an attributed
/// function, or one an attribute argument names.
fn function_params(table: &AttributeTable, name: &str) -> Option<Vec<(String, DataType)>> {
    fn in_value(v: &AttributeValue, name: &str) -> Option<Vec<(String, DataType)>> {
        match v {
            AttributeValue::Fn { name: n, params, .. } if n == name => Some(params.clone()),
            AttributeValue::List(items) => items.iter().find_map(|i| in_value(i, name)),
            _ => None,
        }
    }
    table.attributes.iter().find_map(|a| match &a.target {
        AttributeTarget::Fn { name: n, params } if n == name => Some(params.clone()),
        _ => a.args.iter().find_map(|(_, v)| in_value(v, name)),
    })
}

/// (enum name, variant name) of a payload-less variant attribute value.
fn variant(table: &AttributeTable, v: &AttributeValue) -> Option<(String, String)> {
    match v {
        AttributeValue::Variant { enum_id, name } => Some((table.enums[*enum_id as usize].name.clone(), name.clone())),
        _ => None,
    }
}

fn set_error(message: &str) {
    ERROR.with(|e| *e.borrow_mut() = cstr(message));
}

/// Whether the game declares a component of this name: the engine asks
/// while it spawns a scene.
#[no_mangle]
pub extern "C" fn drift_host_is_component(name: *const c_char) -> i32 {
    let Some(name) = from_c(name) else { return 0 };
    COMPONENT_NAMES.with(|n| n.borrow().contains(&name)) as i32
}

/// A game component a scene spawned onto an entity: its JSON, and the
/// file's entity ids mapped to the new entities as a JSON object. It joins
/// the entity when the current system returns, or at the next frame.
#[no_mangle]
pub extern "C" fn drift_host_scene_component(e: u64, name: *const c_char, value: *const c_char, ids: *const c_char) {
    let (Some(name), Some(value), Some(ids)) = (from_c(name), from_c(value), from_c(ids)) else { return };
    SCENE_COMPONENTS.with(|q| q.borrow_mut().push((e, name, value, ids)));
}

/// Compiles `entry` against the `drift` module in `module_dir`. Returns 0 and
/// sets the error on failure.
#[no_mangle]
pub extern "C" fn drift_host_load(entry: *const c_char, module_dir: *const c_char) -> i32 {
    let (Some(entry), Some(dir)) = (from_c(entry), from_c(module_dir)) else { return 0 };
    USE_BUILT.with(|u| u.set(cfg!(target_os = "emscripten")));
    let loaded = std::panic::catch_unwind(|| load(&entry, &dir));
    USE_BUILT.with(|u| u.set(false));
    match loaded {
        Ok(Ok(host)) => {
            let names: Vec<&str> = host.systems.iter().map(|s| s.name.as_str()).collect();
            log(0, &format!("Candela: {} systems: {}", names.len(), names.join(", ")));
            HOST.with(|h| *h.borrow_mut() = Some(host));
            1
        }
        Ok(Err(e)) => {
            set_error(&e);
            0
        }
        Err(_) => {
            set_error("the candela compiler stopped unexpectedly");
            0
        }
    }
}

/// Checks the game at `entry` the way a load reads it, without building or
/// running any of it, and answers what is wrong as a JSON list of
/// diagnostics (see check.rs). `toml` is the drift.toml the engine read, or
/// an empty string. The answer stays valid until the next call.
#[no_mangle]
pub extern "C" fn drift_host_check(entry: *const c_char, module_dir: *const c_char, toml: *const c_char) -> *const c_char {
    let (Some(entry), Some(dir)) = (from_c(entry), from_c(module_dir)) else { return std::ptr::null() };
    let toml = from_c(toml).unwrap_or_default();
    let found = match std::panic::catch_unwind(|| check::check(&entry, &dir, &toml)) {
        Ok(found) => found,
        Err(_) => vec![json!({
            "file": entry, "line": 1, "severity": "error", "code": "drift_check_failed",
            "message": "the candela compiler stopped unexpectedly while checking the game",
        })],
    };
    CHECKED.with(|c| {
        *c.borrow_mut() = cstr(&Json::Array(found).to_string());
        c.borrow().as_ptr()
    })
}

#[no_mangle]
pub extern "C" fn drift_host_error() -> *const c_char {
    ERROR.with(|e| e.borrow().as_ptr())
}

/// Records a collision event of this frame. kind: 0 contact began, 1 ended,
/// 2 sensor overlap began, 3 ended. A contact that began also brings its
/// first point and the normal from a to b.
#[no_mangle]
pub extern "C" fn drift_host_contact(kind: i32, a: u64, b: u64, px: f32, py: f32, nx: f32, ny: f32) {
    HOST.with(|h| {
        if let Some(host) = h.borrow_mut().as_mut() {
            host.contacts.push((kind, a, b, [px as f64, py as f64, nx as f64, ny as f64]));
        }
    });
}

/// Runs one frame of systems (and the startup systems before the first).
#[no_mangle]
pub extern "C" fn drift_host_frame(dt: f64, elapsed: f64, frame: i64, scale: f64, real_dt: f64) {
    HOST.with(|h| {
        let mut h = h.borrow_mut();
        if let Some(host) = h.as_mut() {
            host.time_scale = (scale, real_dt);
            if std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| host.frame((dt, elapsed, frame)))).is_err() {
                log(2, "Candela: a system stopped unexpectedly");
            }
        }
    });
}

/// Compiles the game again from its files and swaps the new program in while
/// the game runs, keeping its entities, script components, resources and
/// states (reload.rs says how values move). Returns 1 when the new program
/// runs, 0 when it does not compile and 2 when it compiles but a value of the
/// running game cannot move into it; in both refusals the old program keeps
/// running and drift_host_error says why.
#[no_mangle]
pub extern "C" fn drift_host_reload(entry: *const c_char, module_dir: *const c_char) -> i32 {
    let (Some(entry), Some(dir)) = (from_c(entry), from_c(module_dir)) else { return 0 };
    match std::panic::catch_unwind(|| reload::reload(&entry, &dir)) {
        Ok(Ok(notes)) => {
            let names: Vec<String> = HOST.with(|h| {
                h.borrow().as_ref().map(|host| host.systems.iter().map(|s| s.name.clone()).collect()).unwrap_or_default()
            });
            log(0, &format!("Candela: reloaded {entry}; {} systems: {}", names.len(), names.join(", ")));
            for note in notes {
                log(1, &format!("Candela: reload: {note}"));
            }
            1
        }
        Ok(Err(reload::Refused::Compile(e))) => {
            set_error(&e);
            0
        }
        Ok(Err(reload::Refused::Layout(e))) => {
            set_error(&e);
            2
        }
        Err(_) => {
            set_error("the candela compiler stopped unexpectedly");
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn drift_host_unload() {
    HOST.with(|h| *h.borrow_mut() = None);
    RESOURCE_VIEW.with(|r| r.borrow_mut().clear());
}

#[cfg(test)]
mod tests {
    use super::marked_in;

    #[test]
    fn marked_structs_are_read_from_source() {
        let src = r#"
import "drift" { * };
// @component struct Commented {}
/* @resource struct Hidden {} */
@component struct Dice { change: Timer = repeating_timer(0.5) }
@settings(section: "dungeon")
/// Tuning.
struct DungeonSettings { tiles: string[] = ["a(b)", "c"] }
@drift::message
export struct Hit { target: Entity }
@system(schedule: Schedule::Startup)
fn setup() {}
@state enum Phase { Title, Play }
@drift::component(requires: [Transform]) struct Torch {}
struct Plain {}
"#;
        assert_eq!(marked_in(src), ["Dice", "DungeonSettings", "Hit", "Torch"]);
    }
}
