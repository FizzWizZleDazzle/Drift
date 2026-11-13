//! The bytes the host and the engine pass each other: MessagePack, the
//! same values JSON would hold, without turning numbers into text and back.
//! Structs travel as maps by field name, enums as their variant name (or
//! `{variant, payload}` when they carry data).

use candela::Value;
use serde_json::{Map as JsonMap, Number, Value as Json};

fn write_len(out: &mut Vec<u8>, n: usize, fix: u8, fix_max: usize, b16: u8, b32: u8) {
    if n <= fix_max {
        out.push(fix | n as u8);
    } else if n <= 0xffff {
        out.push(b16);
        out.extend_from_slice(&(n as u16).to_be_bytes());
    } else {
        out.push(b32);
        out.extend_from_slice(&(n as u32).to_be_bytes());
    }
}

fn write_str(out: &mut Vec<u8>, s: &str) {
    let n = s.len();
    if n <= 31 {
        out.push(0xa0 | n as u8);
    } else if n <= 0xff {
        out.push(0xd9);
        out.push(n as u8);
    } else {
        write_len(out, n, 0, 0, 0xda, 0xdb);
    }
    out.extend_from_slice(s.as_bytes());
}

fn write_int(out: &mut Vec<u8>, i: i64) {
    if (0..=127).contains(&i) {
        out.push(i as u8);
    } else if (-32..0).contains(&i) {
        out.push(i as i8 as u8);
    } else {
        out.push(0xd3);
        out.extend_from_slice(&i.to_be_bytes());
    }
}

fn write_uint(out: &mut Vec<u8>, u: u64) {
    if u <= 127 {
        out.push(u as u8);
    } else {
        out.push(0xcf);
        out.extend_from_slice(&u.to_be_bytes());
    }
}

fn write_float(out: &mut Vec<u8>, f: f64) {
    out.push(0xcb);
    out.extend_from_slice(&f.to_be_bytes());
}

fn write_array_len(out: &mut Vec<u8>, n: usize) {
    write_len(out, n, 0x90, 15, 0xdc, 0xdd);
}

fn write_map_len(out: &mut Vec<u8>, n: usize) {
    write_len(out, n, 0x80, 15, 0xde, 0xdf);
}

/// A JSON value as MessagePack.
pub fn write_json(out: &mut Vec<u8>, j: &Json) {
    match j {
        Json::Null => out.push(0xc0),
        Json::Bool(b) => out.push(if *b { 0xc3 } else { 0xc2 }),
        Json::Number(n) => {
            if let Some(i) = n.as_i64() {
                write_int(out, i);
            } else if let Some(u) = n.as_u64() {
                write_uint(out, u);
            } else {
                write_float(out, n.as_f64().unwrap_or(0.0));
            }
        }
        Json::String(s) => write_str(out, s),
        Json::Array(items) => {
            write_array_len(out, items.len());
            for item in items {
                write_json(out, item);
            }
        }
        Json::Object(o) => {
            write_map_len(out, o.len());
            for (k, v) in o {
                write_str(out, k);
                write_json(out, v);
            }
        }
    }
}

/// A candela value as MessagePack, shaped as `to_json` shapes it.
pub fn write_value(out: &mut Vec<u8>, v: &Value) {
    match v {
        Value::Null => out.push(0xc0),
        Value::Int(i) => write_int(out, *i),
        // JSON has no NaN or infinity: to_json makes them null.
        Value::Float(f) if f.is_finite() => write_float(out, *f),
        Value::Float(_) => out.push(0xc0),
        Value::Bool(b) => out.push(if *b { 0xc3 } else { 0xc2 }),
        Value::String(s) => write_str(out, s),
        Value::Array(items) => write_values(out, items),
        Value::Map(m) => {
            write_map_len(out, m.len());
            for (k, x) in m {
                write_str(out, k);
                write_value(out, x);
            }
        }
        Value::Struct { fields, .. } => {
            write_map_len(out, fields.len());
            for (k, x) in fields {
                write_str(out, k);
                write_value(out, x);
            }
        }
        Value::Enum { variant, payload } => {
            if payload.is_empty() {
                write_str(out, variant);
            } else {
                write_map_len(out, 2);
                write_str(out, "variant");
                write_str(out, variant);
                write_str(out, "payload");
                write_array_len(out, payload.len());
                for x in payload {
                    write_value(out, x);
                }
            }
        }
    }
}

/// A list of candela values as a MessagePack array.
pub fn write_values(out: &mut Vec<u8>, items: &[Value]) {
    write_array_len(out, items.len());
    for item in items {
        write_value(out, item);
    }
}

struct Reader<'a> {
    bytes: &'a [u8],
    at: usize,
}

impl Reader<'_> {
    fn take(&mut self, n: usize) -> Option<&[u8]> {
        let end = self.at.checked_add(n)?;
        let s = self.bytes.get(self.at..end)?;
        self.at = end;
        Some(s)
    }

    fn byte(&mut self) -> Option<u8> {
        self.take(1).map(|b| b[0])
    }

    fn be<const N: usize>(&mut self) -> Option<[u8; N]> {
        self.take(N)?.try_into().ok()
    }

    fn uint(&mut self, size: usize) -> Option<u64> {
        Some(match size {
            1 => u64::from(self.byte()?),
            2 => u64::from(u16::from_be_bytes(self.be()?)),
            4 => u64::from(u32::from_be_bytes(self.be()?)),
            _ => u64::from_be_bytes(self.be()?),
        })
    }

    fn string(&mut self, n: usize) -> Option<String> {
        Some(String::from_utf8_lossy(self.take(n)?).into_owned())
    }

    fn array(&mut self, n: usize) -> Option<Json> {
        let mut items = Vec::with_capacity(n.min(4096));
        for _ in 0..n {
            items.push(self.value()?);
        }
        Some(Json::Array(items))
    }

    fn map(&mut self, n: usize) -> Option<Json> {
        let mut o = JsonMap::new();
        for _ in 0..n {
            let k = match self.value()? {
                Json::String(s) => s,
                other => other.to_string(),
            };
            o.insert(k, self.value()?);
        }
        Some(Json::Object(o))
    }

    fn value(&mut self) -> Option<Json> {
        let b = self.byte()?;
        Some(match b {
            0x00..=0x7f => Json::from(b),
            0x80..=0x8f => self.map(usize::from(b & 0x0f))?,
            0x90..=0x9f => self.array(usize::from(b & 0x0f))?,
            0xa0..=0xbf => Json::String(self.string(usize::from(b & 0x1f))?),
            0xc0 => Json::Null,
            0xc2 => Json::Bool(false),
            0xc3 => Json::Bool(true),
            // Binary: kept as a list of byte values.
            0xc4..=0xc6 => {
                let n = self.uint(1 << (b - 0xc4))? as usize;
                Json::Array(self.take(n)?.iter().map(|x| Json::from(*x)).collect())
            }
            0xca => Number::from_f64(f64::from(f32::from_be_bytes(self.be()?))).map_or(Json::Null, Json::Number),
            0xcb => Number::from_f64(f64::from_be_bytes(self.be()?)).map_or(Json::Null, Json::Number),
            0xcc..=0xcf => Json::from(self.uint(1 << (b - 0xcc))?),
            0xd0 => Json::from(i64::from(self.byte()? as i8)),
            0xd1 => Json::from(i64::from(i16::from_be_bytes(self.be()?))),
            0xd2 => Json::from(i64::from(i32::from_be_bytes(self.be()?))),
            0xd3 => Json::from(i64::from_be_bytes(self.be()?)),
            0xd9..=0xdb => {
                let n = self.uint(1 << (b - 0xd9))? as usize;
                Json::String(self.string(n)?)
            }
            0xdc | 0xdd => {
                let n = self.uint(if b == 0xdc { 2 } else { 4 })? as usize;
                self.array(n)?
            }
            0xde | 0xdf => {
                let n = self.uint(if b == 0xde { 2 } else { 4 })? as usize;
                self.map(n)?
            }
            0xe0..=0xff => Json::from(i64::from(b as i8)),
            // Extension types: nothing the engine sends.
            _ => return None,
        })
    }
}

/// The value a MessagePack document holds, or None when it is cut short or
/// holds something else after it.
pub fn read_json(bytes: &[u8]) -> Option<Json> {
    let mut r = Reader { bytes, at: 0 };
    let v = r.value()?;
    (r.at == bytes.len()).then_some(v)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn json_round_trips() {
        let long = "x".repeat(300);
        let big: Vec<Json> = (0..40).map(Json::from).collect();
        let j = json!({
            "a": 1, "b": -1, "c": -100, "d": 70000, "e": 1.5, "f": "text", "g": null, "h": [true, false],
            "i": u64::MAX, "j": i64::MIN, "k": long, "l": big, "m": {"nested": {"deep": [1.25, -0.5]}}
        });
        let mut out = Vec::new();
        write_json(&mut out, &j);
        assert_eq!(read_json(&out), Some(j));
    }

    #[test]
    fn values_write_as_to_json_does() {
        let v = Value::Struct {
            name: "Transform".into(),
            fields: vec![
                ("position".into(), Value::Struct { name: "Vec2".into(), fields: vec![("x".into(), Value::Float(1.0)), ("y".into(), Value::Float(-2.5))] }),
                ("kind".into(), Value::Enum { variant: "Dynamic".into(), payload: Vec::new() }),
                ("tags".into(), Value::Array(vec![Value::Int(3), Value::String("a".into())])),
            ],
        };
        let mut out = Vec::new();
        write_value(&mut out, &v);
        assert_eq!(
            read_json(&out),
            Some(json!({"position": {"x": 1.0, "y": -2.5}, "kind": "Dynamic", "tags": [3, "a"]}))
        );
    }

    #[test]
    fn short_input_is_none() {
        let mut out = Vec::new();
        write_json(&mut out, &json!([1, 2, 3]));
        assert_eq!(read_json(&out[..out.len() - 1]), None);
    }
}
