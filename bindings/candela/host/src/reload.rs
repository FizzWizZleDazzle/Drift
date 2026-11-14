//! Hot reload: compiles the game again while it runs and swaps the new
//! program in, keeping the running game.
//!
//! The engine's world is untouched: entities, engine components and engine
//! resources stay as they are. What lives here moves into the new program:
//! script components, script resources, states, messages waiting to be read
//! and whether each system is on. Values are matched to the new structs by
//! field name (see `reshape`). When the new code does not compile, or a
//! value cannot be carried into it, the old program keeps running.

use super::*;

/// Why a reload did not happen.
pub(crate) enum Refused {
    /// The new code does not compile or its systems are not valid.
    Compile(String),
    /// The new code compiles, but a value of the running game does not fit
    /// it, such as a field whose type changed while it holds a value the
    /// game set.
    Layout(String),
}

/// The thread-local state `load` replaces, so a refused reload can put the
/// running game's back.
struct Saved {
    stores: HashMap<String, BTreeMap<u64, Value>>,
    lookup_names: Vec<String>,
    types: Option<std::rc::Rc<Types>>,
    component_names: HashSet<String>,
    switches: HashMap<String, bool>,
    resource_view: HashMap<String, Value>,
}

impl Saved {
    fn take() -> Saved {
        Saved {
            stores: STORES.with(|s| std::mem::take(&mut *s.borrow_mut())),
            lookup_names: LOOKUP_NAMES.with(|s| std::mem::take(&mut *s.borrow_mut())),
            types: TYPES.with(|s| s.borrow_mut().take()),
            component_names: COMPONENT_NAMES.with(|s| std::mem::take(&mut *s.borrow_mut())),
            switches: SWITCHES.with(|s| std::mem::take(&mut *s.borrow_mut())),
            resource_view: RESOURCE_VIEW.with(|s| std::mem::take(&mut *s.borrow_mut())),
        }
    }

    fn restore(self) {
        STORES.with(|s| *s.borrow_mut() = self.stores);
        LOOKUP_NAMES.with(|s| *s.borrow_mut() = self.lookup_names);
        TYPES.with(|s| *s.borrow_mut() = self.types);
        COMPONENT_NAMES.with(|s| *s.borrow_mut() = self.component_names);
        SWITCHES.with(|s| *s.borrow_mut() = self.switches);
        RESOURCE_VIEW.with(|s| *s.borrow_mut() = self.resource_view);
    }
}

/// Compiles `entry` again and, when that works, moves the running game into
/// the new program. Returns what the log should say about it.
pub(crate) fn reload(entry: &str, module_dir: &str) -> Result<Vec<String>, Refused> {
    let Some(old) = HOST.with(|h| h.try_borrow_mut().ok().and_then(|mut h| h.take())) else {
        return Err(Refused::Compile(String::from("the game is not running")));
    };
    let saved = Saved::take();
    let outcome = match load(entry, module_dir) {
        Ok(mut new) => match new.take_over(&old, &saved.stores) {
            Ok(notes) => Ok((new, notes)),
            Err(why) => Err(Refused::Layout(why)),
        },
        Err(why) => Err(Refused::Compile(why)),
    };
    match outcome {
        Ok((new, notes)) => {
            HOST.with(|h| *h.borrow_mut() = Some(new));
            Ok(notes)
        }
        Err(refused) => {
            saved.restore();
            HOST.with(|h| *h.borrow_mut() = Some(old));
            Err(refused)
        }
    }
}

impl Host {
    /// Moves the running game `old` into this newly loaded program. `stores`
    /// are the old program's script components. Returns notes for the log,
    /// or the value that cannot move.
    fn take_over(&mut self, old: &Host, stores: &HashMap<String, BTreeMap<u64, Value>>) -> Result<Vec<String>, String> {
        let mut notes = Vec::new();

        // Script components, entity by entity.
        let mut moved: HashMap<String, BTreeMap<u64, Value>> = HashMap::new();
        for (name, store) in stores {
            if store.is_empty() {
                continue;
            }
            if !self.components.contains(name) {
                notes.push(format!("{} is no longer a component; {} entities lost it", bare(name), store.len()));
                continue;
            }
            let (Some(o), Some(n)) = (old.types.struct_id(name), self.types.struct_id(name)) else { continue };
            let (od, nd) = (old.component_defaults.get(name), self.component_defaults.get(name));
            let mut out = BTreeMap::new();
            for (e, v) in store {
                let v = reshape(&old.types, &DataType::Struct(o), &self.types, &DataType::Struct(n), v, od, nd, bare(name))?;
                out.insert(*e, v);
            }
            moved.insert(name.clone(), out);
        }

        // Script resources.
        let mut resources = HashMap::new();
        for (name, v) in &old.resources {
            if !self.resource_kinds.contains(name) {
                notes.push(format!("{} is no longer a resource; its value is dropped", bare(name)));
                continue;
            }
            let (Some(o), Some(n)) = (old.types.struct_id(name), self.types.struct_id(name)) else { continue };
            let (od, nd) = (old.initial_resources.get(name), self.initial_resources.get(name));
            resources.insert(name.clone(), reshape(&old.types, &DataType::Struct(o), &self.types, &DataType::Struct(n), v, od, nd, bare(name))?);
        }
        // Nothing failed: the new program takes the values.
        STORES.with(|s| {
            let mut s = s.borrow_mut();
            for (name, store) in moved {
                s.insert(name, store);
            }
        });
        for (name, v) in resources {
            publish_resource(&name, Some(&v));
            self.resources.insert(name, v);
        }

        // States keep where they are while their variant still exists. A
        // new state enum, or one whose variant went, starts at its first
        // variant and runs its on_enter systems at the next frame.
        let names: Vec<String> = self.states.keys().cloned().collect();
        for name in names {
            let kept = old.states.get(&name).filter(|o| self.variants(&name).iter().any(|v| v == &o.now));
            let st = self.states.get_mut(&name).expect("listed above");
            match kept {
                Some(o) => {
                    st.now = o.now.clone();
                    st.since = o.since;
                    st.next = o.next.clone();
                }
                None => {
                    if old.started {
                        self.pending_enter.push((name.clone(), st.now.clone()));
                    }
                    if let Some(o) = old.states.get(&name) {
                        notes.push(format!("state {name} was {}, which is gone; it starts at {}", o.now, st.now));
                    }
                }
            }
        }

        // Systems keep being on or off where the game switched them; one
        // that failed runs again with its new code. Messages sent before a
        // new system existed are not delivered to it.
        let mut fresh = Vec::new();
        for sys in self.systems.iter_mut() {
            match old.systems.iter().position(|o| o.name == sys.name) {
                Some(at) => {
                    let o = &old.systems[at];
                    let switched = old.initial_enabled.get(at).is_some_and(|initial| *initial != o.enabled);
                    if switched && !old.failed.contains(&sys.name) {
                        sys.enabled = o.enabled;
                    }
                    sys.seen = o.seen;
                }
                None => {
                    sys.seen = old.next_seq;
                    if sys.startup && old.started {
                        fresh.push(sys.name.clone());
                    }
                }
            }
        }
        if !fresh.is_empty() {
            notes.push(format!("new startup systems run once: {}", fresh.join(", ")));
        }
        self.pending_startup = fresh;
        SWITCHES.with(|w| *w.borrow_mut() = self.systems.iter().map(|s| (s.name.clone(), s.enabled)).collect());

        // Messages still to be read, and ones sent for later.
        let carry = |v: &Value| -> Option<Value> {
            let Value::Struct { name, .. } = v else { return None };
            if !self.messages_kinds.contains(name) {
                return None;
            }
            let (o, n) = (old.types.struct_id(name)?, self.types.struct_id(name)?);
            let (od, nd) = (old.message_defaults.get(name), self.message_defaults.get(name));
            reshape(&old.types, &DataType::Struct(o), &self.types, &DataType::Struct(n), v, od, nd, bare(name)).ok()
        };
        self.messages = old.messages.iter().filter_map(|(seq, name, v)| Some((*seq, name.clone(), carry(v)?))).collect();
        self.later = old.later.iter().filter_map(|(t, v)| Some((*t, carry(v)?))).collect();
        self.next_seq = old.next_seq;
        self.contacts = old.contacts.clone();
        self.started = old.started;
        self.restart_pending = old.restart_pending;
        self.time_scale = old.time_scale;
        Ok(notes)
    }

    /// The variants of the @drift::state enum `name`.
    fn variants(&self, name: &str) -> Vec<String> {
        self.types.table.enums.iter().find(|e| bare(&e.name) == name).map(|e| e.variants.clone()).unwrap_or_default()
    }
}

/// `v`, a value of `old_ty` in the old program, as a value of `new_ty` in the
/// new one. A value still equal to its old default takes the new default, so
/// changing a default in the code reaches values the game never changed.
/// Struct fields match by name: a field both have keeps its value (by the
/// same rule, field by field), a new field takes its default, and a field
/// the new struct does not have is dropped. A field whose type changed while
/// it holds a value the game set cannot move; `path` names it in the error.
#[allow(clippy::too_many_arguments)]
fn reshape(
    old_types: &Types,
    old_ty: &DataType,
    new_types: &Types,
    new_ty: &DataType,
    v: &Value,
    old_default: Option<&Value>,
    new_default: Option<&Value>,
    path: &str,
) -> Result<Value, String> {
    if let (Some(od), Some(nd)) = (old_default, new_default) {
        if v == od {
            return Ok(nd.clone());
        }
    }
    let old_name = old_types.table.type_name(old_ty);
    let new_name = new_types.table.type_name(new_ty);
    if bare_type(&old_name) != bare_type(&new_name) {
        return Err(format!("{path} changed from {old_name} to {new_name} and holds a value the game set"));
    }
    match (old_ty, new_ty, v) {
        (DataType::Struct(o), DataType::Struct(n), Value::Struct { fields, .. }) => {
            let (oi, ni) = (&old_types.table.structs[*o as usize], &new_types.table.structs[*n as usize]);
            let mut out = Vec::with_capacity(ni.fields.len());
            for (f, nt) in &ni.fields {
                let nd = new_default.and_then(|d| field(d, f));
                let before = oi.fields.iter().find(|(of, _)| of == f).zip(fields.iter().find(|(of, _)| of == f));
                let value = match before {
                    Some(((_, ot), (_, ov))) => {
                        let od = old_default.and_then(|d| field(d, f));
                        reshape(old_types, ot, new_types, nt, ov, od, nd, &format!("{path}.{f}"))?
                    }
                    None => nd.cloned().unwrap_or_else(|| new_types.from_json(&Json::Null, nt)),
                };
                out.push((f.clone(), value));
            }
            Ok(Value::Struct { name: ni.name.clone(), fields: out })
        }
        (DataType::Array(Some(oe)), DataType::Array(Some(ne)), Value::Array(items)) => Ok(Value::Array(
            items
                .iter()
                .enumerate()
                .map(|(i, item)| reshape(old_types, oe, new_types, ne, item, None, None, &format!("{path}[{i}]")))
                .collect::<Result<_, _>>()?,
        )),
        (DataType::Map(okv), DataType::Map(nkv), Value::Map(m)) => match (&okv.1, &nkv.1) {
            (Some(ov), Some(nv)) => Ok(Value::Map(
                m.iter()
                    .map(|(k, item)| Ok((k.clone(), reshape(old_types, ov, new_types, nv, item, None, None, &format!("{path}[{k}]"))?)))
                    .collect::<Result<_, String>>()?,
            )),
            _ => Ok(v.clone()),
        },
        (_, DataType::Enum(n), Value::Enum { variant, .. }) => {
            let info = &new_types.table.enums[*n as usize];
            if info.variants.iter().any(|x| x == variant) {
                Ok(v.clone())
            } else {
                Ok(new_default.cloned().unwrap_or_else(|| new_types.from_json(&Json::Null, new_ty)))
            }
        }
        _ => Ok(v.clone()),
    }
}
