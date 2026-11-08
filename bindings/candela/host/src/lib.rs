//! Runs a Candela game inside Drift.
//!
//! The host compiles the game's entry script with the `drift` module, reads
//! the `@drift` attributes, and then, once a frame, calls every system with
//! the data its parameters ask for: query rows, lookups, resources, messages,
//! collisions and engine resources. Components and resources of the script
//! live here, as candela values; engine components live in the engine and
//! cross through the C++ bridge as JSON whose shape matches the `drift`
//! module's structs. What a system wrote comes back with `call_in_out`, and
//! only the types its `writes:` names are kept.

use candela::{
    AttributeTable, AttributeTarget, AttributeValue, DataType, Diagnostic, Engine, HostError, Program,
    Value,
};
use serde_json::{json, Map as JsonMap, Value as Json};
use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::ffi::{c_char, CStr, CString};

// ---- The C++ side ----

extern "C" {
    fn drift_bridge_log(level: i32, message: *const c_char);
    fn drift_bridge_new() -> u64;
    fn drift_bridge_despawn(e: u64);
    fn drift_bridge_alive(e: u64) -> i32;
    fn drift_bridge_is_engine(component: *const c_char) -> i32;
    fn drift_bridge_has(e: u64, component: *const c_char) -> i32;
    fn drift_bridge_get(e: u64, component: *const c_char) -> *const c_char;
    fn drift_bridge_set(e: u64, component: *const c_char, json: *const c_char) -> i32;
    fn drift_bridge_remove(e: u64, component: *const c_char);
    fn drift_bridge_list(component: *const c_char) -> *const c_char;
    fn drift_bridge_call(function: *const c_char, args: *const c_char) -> *const c_char;
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

fn is_engine(component: &str) -> bool {
    let c = cstr(component);
    unsafe { drift_bridge_is_engine(c.as_ptr()) != 0 }
}

fn engine_get(e: u64, component: &str) -> Option<Json> {
    let c = cstr(component);
    let text = from_c(unsafe { drift_bridge_get(e, c.as_ptr()) })?;
    serde_json::from_str(&text).ok()
}

fn engine_set(e: u64, component: &str, value: &Json) {
    let c = cstr(component);
    let j = cstr(&value.to_string());
    unsafe { drift_bridge_set(e, c.as_ptr(), j.as_ptr()) };
}

fn engine_list(component: &str) -> Vec<u64> {
    let c = cstr(component);
    from_c(unsafe { drift_bridge_list(c.as_ptr()) })
        .and_then(|t| serde_json::from_str::<Vec<u64>>(&t).ok())
        .unwrap_or_default()
}

/// Calls a bridge function with JSON arguments and reads its JSON answer.
fn call(function: &str, args: Json) -> Json {
    let f = cstr(function);
    let a = cstr(&args.to_string());
    from_c(unsafe { drift_bridge_call(f.as_ptr(), a.as_ptr()) })
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or(Json::Null)
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
}

impl Types {
    fn struct_name(&self, id: u16) -> &str {
        &self.table.structs[id as usize].name
    }

    fn struct_id(&self, name: &str) -> Option<u16> {
        self.table.structs.iter().position(|s| s.name == name).map(|i| i as u16)
    }

    /// A value of type `ty` read from JSON; what JSON leaves out takes the
    /// zero value of its type.
    fn from_json(&self, j: &Json, ty: &DataType) -> Value {
        match ty {
            DataType::Int => Value::Int(j.as_i64().or_else(|| j.as_f64().map(|f| f as i64)).unwrap_or(0)),
            DataType::Float => Value::Float(j.as_f64().unwrap_or(0.0)),
            DataType::Bool => Value::Bool(j.as_bool().unwrap_or(false)),
            DataType::String => Value::String(j.as_str().unwrap_or("").to_owned()),
            DataType::Array(elem) => {
                let elem = elem.as_deref().unwrap_or(&DataType::Unknown);
                Value::Array(j.as_array().map(|a| a.iter().map(|e| self.from_json(e, elem)).collect()).unwrap_or_default())
            }
            DataType::Map(kv) => {
                let elem = kv.1.as_ref().unwrap_or(&DataType::Unknown);
                Value::Map(
                    j.as_object()
                        .map(|o| o.iter().map(|(k, v)| (k.clone(), self.from_json(v, elem))).collect())
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
                        .map(|(f, t)| (f.clone(), self.from_json(obj.get(f).unwrap_or(&Json::Null), t)))
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
                _ => Value::Null,
            },
        }
    }

    /// `base` with the fields `j` names replaced, for settings sections.
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
    Lookup { name: u16, component: String },
    Resource(String),
    Messages(String),
    Contacts { name: u16, kind: i32, a: String, b: String },
    Commands(u16),
    Time(u16),
    Engine(u16),
}

#[derive(Clone, Debug)]
struct System {
    name: String,
    params: Vec<Param>,
    writes: HashSet<String>,
    startup: bool,
    enabled: bool,
    after: Vec<String>,
    before: Vec<String>,
    /// Messages up to this sequence number have been delivered.
    seen: u64,
}

enum Command {
    Spawn(u64, Vec<Value>),
    Insert(u64, Value),
    Remove(u64, String),
    Despawn(u64),
    InsertResource(Value),
    RemoveResource(String),
    Send(Value),
}

thread_local! {
    static QUEUE: RefCell<Vec<Command>> = const { RefCell::new(Vec::new()) };
    static HOST: RefCell<Option<Host>> = const { RefCell::new(None) };
    static ERROR: RefCell<CString> = RefCell::new(CString::default());
}

fn queue(c: Command) {
    QUEUE.with(|q| q.borrow_mut().push(c));
}

struct Host {
    program: Program,
    types: Types,
    systems: Vec<System>,
    components: HashSet<String>,
    messages_kinds: HashSet<String>,
    resource_kinds: HashSet<String>,
    /// Script components: type name -> entity -> value.
    stores: HashMap<String, BTreeMap<u64, Value>>,
    resources: HashMap<String, Value>,
    messages: Vec<(u64, String, Value)>,
    next_seq: u64,
    contacts: Vec<(i32, u64, u64)>,
    started: bool,
    reported: HashSet<String>,
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
            AttributeValue::Fn(name) => Some(name.clone()),
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
                let info = &types.table.structs[id as usize];
                let mut fields = Vec::new();
                for (f, t) in &info.fields {
                    let DataType::Struct(cid) = t else { return Err(format!("row field {f} is not a component")) };
                    let cname = types.struct_name(*cid).to_owned();
                    if bare(&cname) == "Entity" {
                        fields.push((f.clone(), None));
                    } else if self.is_component(&cname) {
                        fields.push((f.clone(), Some(cname)));
                    } else {
                        return Err(format!("{cname} in {} is not a component", info.name));
                    }
                }
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
                        return Ok(Param::Lookup { name: *id, component: args[0].clone() });
                    }
                }
                match bare(&name) {
                    "Commands" => Ok(Param::Commands(*id)),
                    "Time" => Ok(Param::Time(*id)),
                    "Input" | "Assets" | "Window" | "Effects" => Ok(Param::Engine(*id)),
                    _ => Err(format!("{name} is not a resource, a query, a lookup or an engine resource")),
                }
            }
            other => Err(format!("{} is not a system parameter", types.table.type_name(other))),
        }
    }

    fn component(&self, e: u64, name: &str) -> Option<Value> {
        if let Some(store) = self.stores.get(name) {
            return store.get(&e).cloned();
        }
        let id = self.types.struct_id(name)?;
        let j = engine_get(e, name)?;
        Some(self.types.from_json(&j, &DataType::Struct(id)))
    }

    fn has(&self, e: u64, name: &str) -> bool {
        match self.stores.get(name) {
            Some(store) => store.contains_key(&e),
            None if self.components.contains(name) => false,
            None => {
                let c = cstr(name);
                unsafe { drift_bridge_has(e, c.as_ptr()) != 0 }
            }
        }
    }

    fn entities_with(&self, name: &str) -> Vec<u64> {
        match self.stores.get(name) {
            Some(store) => store.keys().copied().collect(),
            None if self.components.contains(name) => Vec::new(),
            None => engine_list(name),
        }
    }

    fn set_component(&mut self, e: u64, value: Value) {
        let Value::Struct { name, .. } = &value else { return };
        let name = name.clone();
        if self.components.contains(&name) {
            self.stores.entry(name).or_default().insert(e, value);
        } else if is_engine(&name) {
            engine_set(e, &name, &to_json(&value));
        } else if self.reported.insert(format!("bundle:{name}")) {
            log(1, &format!("{name} is not a component: mark it @drift::component"));
        }
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
                    _ => self.types.from_json(&Json::Null, t),
                };
                (f.clone(), v)
            })
            .collect();
        Value::Struct { name, fields }
    }

    /// Builds the arguments of one system and what each row came from.
    fn arguments(&self, sys: &System, time: (f64, f64, i64)) -> Option<(Vec<Value>, Vec<Vec<u64>>)> {
        let mut args = Vec::new();
        let mut entities = Vec::new();
        for p in &sys.params {
            let mut ids = Vec::new();
            let v = match p {
                Param::Rows { row, fields, with, without } => {
                    let mut needed: Vec<String> = fields.iter().filter_map(|(_, c)| c.clone()).collect();
                    needed.extend(with.iter().cloned());
                    let row_name = self.types.struct_name(*row).to_owned();
                    let mut rows = Vec::new();
                    for e in self.matching(&needed, without) {
                        let mut values = Vec::new();
                        for (f, c) in fields {
                            let v = match c {
                                None => self.entity_value(e),
                                Some(c) => self.component(e, c)?,
                            };
                            values.push((f.clone(), v));
                        }
                        rows.push(Value::Struct { name: row_name.clone(), fields: values });
                        ids.push(e);
                    }
                    Value::Array(rows)
                }
                Param::Lookup { name, component } => {
                    let mut idv = Vec::new();
                    let mut rows = Vec::new();
                    for e in self.entities_with(component) {
                        if let Some(v) = self.component(e, component) {
                            idv.push(Value::Int(e as i64));
                            rows.push(v);
                            ids.push(e);
                        }
                    }
                    Value::Struct {
                        name: self.types.struct_name(*name).to_owned(),
                        fields: vec![("ids".into(), Value::Array(idv)), ("rows".into(), Value::Array(rows))],
                    }
                }
                Param::Resource(name) => self.resources.get(name)?.clone(),
                Param::Messages(name) => Value::Array(
                    self.messages.iter().filter(|(seq, n, _)| *seq > sys.seen && n == name).map(|(_, _, v)| v.clone()).collect(),
                ),
                Param::Contacts { name, kind, a, b } => {
                    let row = self.types.struct_name(*name).to_owned();
                    let mut out = Vec::new();
                    for &(k, x, y) in &self.contacts {
                        if k != *kind {
                            continue;
                        }
                        let pair = if self.has(x, a) && self.has(y, b) {
                            Some((x, y))
                        } else if self.has(y, a) && self.has(x, b) {
                            Some((y, x))
                        } else {
                            None
                        };
                        if let Some((ea, eb)) = pair {
                            out.push(Value::Struct {
                                name: row.clone(),
                                fields: vec![("a".into(), self.entity_value(ea)), ("b".into(), self.entity_value(eb))],
                            });
                        }
                    }
                    Value::Array(out)
                }
                Param::Commands(id) => Value::Struct { name: self.types.struct_name(*id).to_owned(), fields: vec![("id".into(), Value::Int(0))] },
                Param::Time(id) => Value::Struct {
                    name: self.types.struct_name(*id).to_owned(),
                    fields: vec![
                        ("dt".into(), Value::Float(time.0)),
                        ("elapsed".into(), Value::Float(time.1)),
                        ("frame".into(), Value::Int(time.2)),
                    ],
                },
                Param::Engine(id) => self.engine_resource(*id),
            };
            args.push(v);
            entities.push(ids);
        }
        Some((args, entities))
    }

    /// Keeps what the system wrote to the types it declared.
    fn write_back(&mut self, sys: &System, before: &[Value], after: &[Value], entities: &[Vec<u64>]) {
        for (i, p) in sys.params.iter().enumerate() {
            let (old, new) = (&before[i], &after[i]);
            if old == new {
                continue;
            }
            match p {
                Param::Rows { fields, .. } => {
                    let (Value::Array(o), Value::Array(n)) = (old, new) else { continue };
                    for (r, e) in entities[i].iter().enumerate() {
                        let (Some(Value::Struct { fields: of, .. }), Some(Value::Struct { fields: nf, .. })) = (o.get(r), n.get(r)) else { continue };
                        for (k, (f, c)) in fields.iter().enumerate() {
                            let Some(c) = c else { continue };
                            if of[k].1 == nf[k].1 {
                                continue;
                            }
                            if sys.writes.contains(c) {
                                self.set_component(*e, nf[k].1.clone());
                            } else {
                                self.undeclared(sys, c, f);
                            }
                        }
                    }
                }
                Param::Lookup { component, .. } => {
                    let (Some(Value::Array(o)), Some(Value::Array(n))) = (field(old, "rows"), field(new, "rows")) else { continue };
                    for (r, e) in entities[i].iter().enumerate() {
                        if o.get(r) == n.get(r) {
                            continue;
                        }
                        if sys.writes.contains(component) {
                            if let Some(v) = n.get(r) {
                                self.set_component(*e, v.clone());
                            }
                        } else {
                            self.undeclared(sys, component, "lookup");
                        }
                    }
                }
                Param::Resource(name) => {
                    if sys.writes.contains(name) {
                        self.resources.insert(name.clone(), new.clone());
                    } else {
                        self.undeclared(sys, name, "resource");
                    }
                }
                _ => {}
            }
        }
    }

    fn undeclared(&mut self, sys: &System, ty: &str, _what: &str) {
        if self.reported.insert(format!("{}:{ty}", sys.name)) {
            log(1, &format!("{} changed {ty} without writes: [{ty}]; the change is discarded", sys.name));
        }
    }

    fn apply(&mut self) {
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
                    if let Some(store) = self.stores.get_mut(&name) {
                        store.remove(&e);
                    } else {
                        let c = cstr(&name);
                        unsafe { drift_bridge_remove(e, c.as_ptr()) };
                    }
                }
                Command::Despawn(e) => {
                    for store in self.stores.values_mut() {
                        store.remove(&e);
                    }
                    unsafe { drift_bridge_despawn(e) };
                }
                Command::InsertResource(v) => {
                    if let Value::Struct { name, .. } = &v {
                        self.resources.insert(name.clone(), v.clone());
                    }
                }
                Command::RemoveResource(name) => {
                    self.resources.remove(&name);
                }
                Command::Send(v) => {
                    if let Value::Struct { name, .. } = &v {
                        self.next_seq += 1;
                        self.messages.push((self.next_seq, name.clone(), v.clone()));
                    }
                }
            }
        }
    }

    fn run(&mut self, index: usize, time: (f64, f64, i64)) {
        let sys = self.systems[index].clone();
        if !sys.enabled {
            return;
        }
        let Some((mut args, entities)) = self.arguments(&sys, time) else { return };
        let before = args.clone();
        let mark = self.next_seq;
        match self.program.call_in_out(&sys.name, &mut args) {
            Ok(_) => {
                self.write_back(&sys, &before, &args, &entities);
                self.apply();
            }
            Err(d) => {
                QUEUE.with(|q| q.borrow_mut().clear());
                log(2, &format!("{} raised {}: {}", sys.name, d.code, d.message));
                self.systems[index].enabled = false;
            }
        }
        self.systems[index].seen = self.systems[index].seen.max(mark);
    }

    fn frame(&mut self, time: (f64, f64, i64)) {
        // Entities the engine removed (a Lifetime ran out, a parent went).
        for store in self.stores.values_mut() {
            store.retain(|e, _| unsafe { drift_bridge_alive(*e) != 0 });
        }
        let order: Vec<usize> = (0..self.systems.len()).collect();
        if !self.started {
            self.started = true;
            for &i in &order {
                if self.systems[i].startup {
                    self.run(i, time);
                }
            }
        }
        for &i in &order {
            if !self.systems[i].startup {
                self.run(i, time);
            }
        }
        // Every system has run since these were sent, or will not read them.
        let oldest = self.systems.iter().filter(|s| !s.startup).map(|s| s.seen).min().unwrap_or(self.next_seq);
        self.messages.retain(|(seq, _, _)| *seq > oldest);
        self.contacts.clear();
        self.program.collect(2000);
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
    let v = |args: &[Value], i: usize| args.get(i).cloned().unwrap_or(Value::Null);
    e.register_host_fn("drift", "entity_new", || unsafe { drift_bridge_new() } as i64);
    e.register_host_fn_variadic("drift", "spawn_bundle", move |a| {
        let id = v(a, 0).as_i64().ok_or_else(|| host_error("spawn needs an entity"))? as u64;
        let bundle = match v(a, 1) {
            Value::Array(items) => items,
            other => vec![other],
        };
        queue(Command::Spawn(id, bundle));
        Ok(Value::Null)
    });
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
    e.register_host_fn("drift", "action_state", move |n: String, s: i64| b(call("action_state", json!([n, s]))));
    e.register_host_fn("drift", "action_value", move |n: &str| f(call("action_value", json!([n]))));
    e.register_host_fn("drift", "action_axis2d", move |n: &str| fv(call("action_axis2d", json!([n]))));
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
    e.register_host_fn("drift", "sound_play", |s: i64, vol: f64, pitch: f64, looping: bool| {
        call("sound_play", json!([s, vol, pitch, looping]));
    });
    e.register_host_fn("drift", "quit", || {
        call("quit", json!([]));
    });
    e.register_host_fn("drift", "window_size", move || fv(call("window_size", json!([]))));
    e.register_host_fn("drift", "flash", |r: f64, g: f64, bl: f64, a: f64, s: f64| {
        call("flash", json!([r, g, bl, a, s]));
    });
    e.register_host_fn("drift", "shake", |c: i64, s: f64| {
        call("shake", json!([c, s]));
    });
    e.register_host_fn("drift", "log_message", |level: i64, m: String| log(level as i32, &m));
    e
}

/// Compiles the game and builds its systems and resources.
fn load(entry: &str, module_dir: &str) -> Result<Host, String> {
    let src = std::fs::read_to_string(entry).map_err(|e| format!("{entry}: {e}"))?;
    let main = if src.contains("fn main(") { String::new() } else { String::from("\nfn main() {}\n") };

    // First the game alone, for its attributes.
    let first = engine(module_dir).compile(&format!("{src}{main}"), entry).map_err(|d| report(&d, entry, &src))?;
    let table = first.attributes();
    let types = Types { table };
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

    // Then with a function that builds each resource from its defaults. A
    // resource with a field that has no default cannot be built; it waits for
    // cmd.insert_resource.
    let glue_for = |list: &[&(u16, Option<String>)]| -> String {
        let mut g = String::new();
        for (id, _) in list {
            let n = types.struct_name(*id);
            g.push_str(&format!("\nfn __drift_default_{id}() -> {n} {{ return {n} {{ .. }}; }}\n"));
        }
        g
    };
    let mut buildable: Vec<&(u16, Option<String>)> = resources_list.iter().collect();
    let program = loop {
        match engine(module_dir).compile(&format!("{src}{main}{}", glue_for(&buildable)), entry) {
            Ok(p) => break p,
            Err(_) if !buildable.is_empty() => {
                // Find the resources whose default does not compile.
                buildable.retain(|r| engine(module_dir).compile(&format!("{src}{main}{}", glue_for(&[r])), entry).is_ok());
                if let Ok(p) = engine(module_dir).compile(&format!("{src}{main}{}", glue_for(&buildable)), entry) {
                    break p;
                }
                buildable.clear();
            }
            Err(d) => return Err(report(&d, entry, &src)),
        }
    };

    let mut host = Host {
        program,
        types,
        systems: Vec::new(),
        components,
        messages_kinds,
        resource_kinds,
        stores: HashMap::new(),
        resources: HashMap::new(),
        messages: Vec::new(),
        next_seq: 0,
        contacts: Vec::new(),
        started: false,
        reported: HashSet::new(),
    };
    for c in &host.components {
        host.stores.insert(c.clone(), BTreeMap::new());
    }
    for (id, section) in buildable.iter().map(|r| (r.0, r.1.clone())) {
        let name = host.types.struct_name(id).to_owned();
        let mut v = host.program.call(&format!("__drift_default_{id}"), &[]).map_err(|d| d.message)?;
        if let Some(section) = section {
            let j = call("settings_section", json!([section]));
            v = host.types.merge(&v, &j, id);
        }
        host.resources.insert(name, v);
    }
    for (id, section) in &settings {
        if !buildable.iter().any(|r| r.0 == *id) {
            log(2, &format!("drift_settings_without_defaults: {} [{}]", host.types.struct_name(*id), section.clone().unwrap_or_default()));
        }
    }

    let mut systems = Vec::new();
    let mut errors = Vec::new();
    for a in host.types.table.named("drift", "system") {
        let AttributeTarget::Fn { name, params } = &a.target else { continue };
        let mut ps = Vec::new();
        for (pname, ty) in params {
            match host.classify(ty) {
                Ok(p) => ps.push(p),
                Err(why) => errors.push(format!("drift_unknown_parameter: {name}({pname}): {why}")),
            }
        }
        let startup = matches!(a.arg("schedule"), Some(AttributeValue::Variant { name: v, .. }) if v == "Startup");
        let enabled = !matches!(a.arg("enabled"), Some(AttributeValue::Bool(false)));
        systems.push(System {
            name: name.clone(),
            params: ps,
            writes: type_list(&host.types, a.arg("writes")).into_iter().collect(),
            startup,
            enabled,
            after: fn_list(a.arg("after")),
            before: fn_list(a.arg("before")),
            seen: 0,
        });
    }
    if !errors.is_empty() {
        return Err(errors.join("\n"));
    }
    host.systems = order(systems)?;
    Ok(host)
}

fn set_error(message: &str) {
    ERROR.with(|e| *e.borrow_mut() = cstr(message));
}

/// Compiles `entry` against the `drift` module in `module_dir`. Returns 0 and
/// sets the error on failure.
#[no_mangle]
pub extern "C" fn drift_host_load(entry: *const c_char, module_dir: *const c_char) -> i32 {
    let (Some(entry), Some(dir)) = (from_c(entry), from_c(module_dir)) else { return 0 };
    match std::panic::catch_unwind(|| load(&entry, &dir)) {
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

#[no_mangle]
pub extern "C" fn drift_host_error() -> *const c_char {
    ERROR.with(|e| e.borrow().as_ptr())
}

/// Records a collision event of this frame. kind: 0 contact began, 1 ended,
/// 2 sensor overlap began, 3 ended.
#[no_mangle]
pub extern "C" fn drift_host_contact(kind: i32, a: u64, b: u64) {
    HOST.with(|h| {
        if let Some(host) = h.borrow_mut().as_mut() {
            host.contacts.push((kind, a, b));
        }
    });
}

/// Runs one frame of systems (and the startup systems before the first).
#[no_mangle]
pub extern "C" fn drift_host_frame(dt: f64, elapsed: f64, frame: i64) {
    HOST.with(|h| {
        let mut h = h.borrow_mut();
        if let Some(host) = h.as_mut() {
            if std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| host.frame((dt, elapsed, frame)))).is_err() {
                log(2, "Candela: a system stopped unexpectedly");
            }
        }
    });
}

#[no_mangle]
pub extern "C" fn drift_host_unload() {
    HOST.with(|h| *h.borrow_mut() = None);
}
