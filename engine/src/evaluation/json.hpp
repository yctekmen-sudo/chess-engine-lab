#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace chess_lab::eval_detail {
using Json = nlohmann::json;
inline void require(bool ok, const char *msg) {
  if (!ok)
    throw std::runtime_error(msg);
}
inline Json parse(std::string_view text) {
  require(!text.empty() && text.size() <= 1048576, "JSON size limit");
  require(text.find(char(0)) == text.npos, "NUL in JSON input");
  std::vector<std::set<std::string>> keys;
  auto j =
      Json::parse(text, [&](int depth, Json::parse_event_t event, Json &value) {
        require(depth <= 64, "JSON nesting limit");
        if (event == Json::parse_event_t::object_start)
          keys.emplace_back();
        if (event == Json::parse_event_t::key)
          require(keys.back().insert(value.get<std::string>()).second,
                  "duplicate JSON key");
        if (event == Json::parse_event_t::object_end)
          keys.pop_back();
        if (event == Json::parse_event_t::value && value.is_number())
          require(std::isfinite(value.get<double>()), "nonfinite JSON");
        return true;
      });
  // dump validates UTF-8 including strings not visited as scalar values.
  (void)j.dump();
  return j;
}
inline std::vector<uint16_t> utf16(const std::string &s) {
  std::vector<uint16_t> out;
  for (size_t i = 0; i < s.size();) {
    unsigned c = (unsigned char)s[i++];
    if (c >= 128) {
      unsigned n = c < 224 ? 1 : c < 240 ? 2 : 3;
      c &= (1u << (6 - n)) - 1;
      while (n--)
        c = (c << 6) | ((unsigned char)s[i++] & 63);
    }
    if (c > 65535) {
      c -= 65536;
      out.push_back(0xd800 + (c >> 10));
      out.push_back(0xdc00 + (c & 1023));
    } else
      out.push_back(c);
  }
  return out;
}
inline size_t scalars(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 192) != 128)
      ++n;
  return n;
}
inline std::string number(double d) {
  require(std::isfinite(d), "nonfinite number");
  if (d == 0)
    return "0";
  char b[128];
  // Request shortest significant digits first. General-format to_chars may
  // prefer an exact fixed integer (e.g. ...0128) over the JCS shortest
  // significant representation (...0100). Apply ECMAScript notation below.
  auto r = std::to_chars(b, b + 128, d, std::chars_format::scientific);
  require(r.ec == std::errc{}, "number conversion");
  std::string s(b, r.ptr);
  bool neg = s[0] == '-';
  if (neg)
    s.erase(0, 1);
  auto e = s.find_first_of("eE");
  int exp = 0;
  if (e != s.npos) {
    exp = std::stoi(s.substr(e + 1));
    s.resize(e);
  }
  auto dot = s.find('.');
  int point = dot == s.npos ? int(s.size()) : int(dot);
  if (dot != s.npos)
    s.erase(dot, 1);
  point += exp;
  while (s.size() > 1 && s[0] == '0') {
    s.erase(0, 1);
    --point;
  }
  while (s.size() > 1 && s.back() == '0')
    s.pop_back();
  std::string o;
  if (point > 0 && point <= 21) {
    o = s;
    if (point >= int(s.size()))
      o.append(point - s.size(), '0');
    else
      o.insert(point, ".");
  } else if (point <= 0 && point > -6)
    o = "0." + std::string(-point, '0') + s;
  else {
    o = s.substr(0, 1);
    if (s.size() > 1)
      o += "." + s.substr(1);
    int p = point - 1;
    o += "e" + std::string(p >= 0 ? "+" : "") + std::to_string(p);
  }
  return (neg ? "-" : "") + o;
}
inline std::string canonical(const Json &j) {
  if (j.is_number())
    return number(j.get<double>());
  if (j.is_object()) {
    std::vector<std::string> keys;
    for (auto it = j.begin(); it != j.end(); ++it)
      keys.push_back(it.key());
    std::sort(keys.begin(), keys.end(),
              [](auto &a, auto &b) { return utf16(a) < utf16(b); });
    std::string o = "{";
    for (auto &k : keys) {
      if (o.size() > 1)
        o += ",";
      o += Json(k).dump() + ":" + canonical(j.at(k));
    }
    return o + "}";
  }
  if (j.is_array()) {
    std::string o = "[";
    for (auto &v : j) {
      if (o.size() > 1)
        o += ",";
      o += canonical(v);
    }
    return o + "]";
  }
  return j.dump();
}
inline bool type(const Json &p, const Json &v) {
  auto t = p.at("type").get<std::string>();
  if (t == "number")
    return v.is_number() && std::isfinite(v.get<double>());
  if (t == "integer")
    return v.is_number() && std::isfinite(v.get<double>()) &&
           std::abs(v.get<double>()) <= 9007199254740991.0 &&
           std::trunc(v.get<double>()) == v.get<double>();
  if (t == "string")
    return v.is_string();
  if (t == "boolean")
    return v.is_boolean();
  return false;
}
inline void value(const Json &p, const Json &v) {
  require(type(p, v), "parameter type mismatch");
  if (p.contains("enum"))
    require(std::find(p["enum"].begin(), p["enum"].end(), v) != p["enum"].end(),
            "parameter enum");
  if (v.is_number()) {
    if (p.contains("minimum"))
      require(v.get<double>() >= p["minimum"].get<double>(), "below minimum");
    if (p.contains("maximum"))
      require(v.get<double>() <= p["maximum"].get<double>(), "above maximum");
  }
  if (v.is_string()) {
    auto n = scalars(v.get<std::string>());
    if (p.contains("minLength"))
      require(n >= p["minLength"].get<uint64_t>(), "string too short");
    if (p.contains("maxLength"))
      require(n <= p["maxLength"].get<uint64_t>(), "string too long");
  }
}
inline void schema(const Json &s) {
  require(s.is_object(), "schema object");
  for (auto it = s.begin(); it != s.end(); ++it)
    require(std::set<std::string>{"schema_version", "type", "properties",
                                  "additionalProperties", "required"}
                .count(it.key()),
            "unknown schema keyword");
  require(s.at("schema_version") == 1 && s.at("type") == "object" &&
              s.at("additionalProperties") == false &&
              s.at("properties").is_object(),
          "schema root");
  std::set<std::string> required;
  if (s.contains("required")) {
    require(s["required"].is_array(), "required array");
    for (auto &k : s["required"]) {
      require(k.is_string() && s["properties"].contains(k.get<std::string>()) &&
                  required.insert(k.get<std::string>()).second,
              "invalid required key");
    }
  }
  for (auto it = s["properties"].begin(); it != s["properties"].end(); ++it) {
    auto &p = it.value();
    require(p.is_object() && p.contains("type") && p["type"].is_string(),
            "property type");
    auto t = p["type"].get<std::string>();
    require(t == "number" || t == "integer" || t == "boolean" || t == "string",
            "unsupported property type");
    for (auto q = p.begin(); q != p.end(); ++q) {
      auto k = q.key();
      require(k == "type" || k == "default" || k == "enum" ||
                  ((t == "number" || t == "integer") &&
                   (k == "minimum" || k == "maximum")) ||
                  (t == "string" && (k == "minLength" || k == "maxLength")),
              "unsupported property keyword");
    }
    for (auto k : {"minimum", "maximum"})
      if (p.contains(k))
        require(p[k].is_number() && std::isfinite(p[k].get<double>()),
                "invalid numeric bound");
    for (auto k : {"minLength", "maxLength"})
      if (p.contains(k))
        require(p[k].is_number_integer() && p[k].get<double>() >= 0 &&
                    p[k].get<double>() <= 9007199254740991.0,
                "invalid length bound");
    if (p.contains("minimum") && p.contains("maximum"))
      require(p["minimum"].get<double>() <= p["maximum"].get<double>(),
              "inverted bounds");
    if (p.contains("minLength") && p.contains("maxLength"))
      require(p["minLength"].get<double>() <= p["maxLength"].get<double>(),
              "inverted lengths");
    if (p.contains("enum")) {
      require(p["enum"].is_array() && !p["enum"].empty(), "enum array");
      std::set<std::string> seen;
      for (auto &v : p["enum"]) {
        value(p, v);
        require(seen.insert(canonical(v)).second, "duplicate enum");
      }
    }
    require(required.count(it.key()) || p.contains("default"),
            "optional default missing");
    if (p.contains("default"))
      value(p, p["default"]);
  }
}
inline Json resolve(const Json &s, const Json &input) {
  require(input.is_object(), "configuration object");
  Json out = input;
  for (auto it = input.begin(); it != input.end(); ++it)
    require(s["properties"].contains(it.key()), "unknown parameter");
  for (auto it = s["properties"].begin(); it != s["properties"].end(); ++it) {
    if (!out.contains(it.key())) {
      bool req = s.contains("required") &&
                 std::find(s["required"].begin(), s["required"].end(),
                           Json(it.key())) != s["required"].end();
      require(!req && it.value().contains("default"),
              "required parameter missing");
      out[it.key()] = it.value()["default"];
    }
    value(it.value(), out[it.key()]);
  }
  return out;
}
} // namespace chess_lab::eval_detail
