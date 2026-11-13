// HTTP requests and WebSockets for Candela games, through the browser's
// fetch() and WebSocket. Answers arrive as messages: HttpResponse for a
// request, SocketEvent for a socket (opened, a message, closed, an error).
// The page's JavaScript queues them as they happen; netPoll() hands them to
// the game once a frame.
//
// Desktop builds have no networking yet: a request answers with an error
// response and a socket with an Error event, so games behave the same way
// when the network is missing.

#include "services.hpp"

#include <drift/Log.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cstdint>
#include <cstdlib>

using nlohmann::json;

namespace drift::candela {

namespace {

int32_t g_next = 1;

#ifdef __EMSCRIPTEN__
EM_JS(void, drift_js_http, (int id, const char* method, const char* url, const char* body, const char* headers), {
    var net = Module.driftNet || (Module.driftNet = {queue: [], sockets: {}});
    var m = UTF8ToString(method);
    var opts = {method: m, headers: JSON.parse(UTF8ToString(headers))};
    if (m !== 'GET' && m !== 'HEAD') opts.body = UTF8ToString(body);
    var request = {id: id};
    try {
        fetch(UTF8ToString(url), opts)
            .then(function (r) {
                return r.text().then(function (t) {
                    net.queue.push({type: 'HttpResponse',
                                    value: {request: request, status: r.status, ok: r.ok, body: t, error: ""}});
                });
            })
            .catch(function (e) {
                net.queue.push({type: 'HttpResponse',
                                value: {request: request, status: 0, ok: false, body: "", error: String(e)}});
            });
    } catch (e) {
        net.queue.push({type: 'HttpResponse', value: {request: request, status: 0, ok: false, body: "", error: String(e)}});
    }
});

EM_JS(void, drift_js_ws_open, (int id, const char* url), {
    var net = Module.driftNet || (Module.driftNet = {queue: [], sockets: {}});
    var event = function (kind, text, code) {
        net.queue.push({type: 'SocketEvent', value: {socket: {id: id}, kind: kind, text: text || "", code: code || 0}});
    };
    var ws;
    try {
        ws = new WebSocket(UTF8ToString(url));
    } catch (e) {
        event('Error', String(e), 0);
        event('Closed', "", 1006);
        return;
    }
    ws.binaryType = 'arraybuffer';
    net.sockets[id] = ws;
    ws.onopen = function () { event('Opened', "", 0); };
    ws.onmessage = function (e) {
        event('Message', typeof e.data === 'string' ? e.data : new TextDecoder().decode(e.data), 0);
    };
    ws.onerror = function () { event('Error', 'connection error', 0); };
    ws.onclose = function (e) {
        delete net.sockets[id];
        event('Closed', e.reason, e.code);
    };
});

EM_JS(int, drift_js_ws_send, (int id, const char* text), {
    var net = Module.driftNet;
    var ws = net && net.sockets[id];
    if (!ws || ws.readyState !== 1) return 0;
    ws.send(UTF8ToString(text));
    return 1;
});

EM_JS(void, drift_js_ws_close, (int id, int code, const char* reason), {
    var net = Module.driftNet;
    var ws = net && net.sockets[id];
    if (ws) ws.close(code, UTF8ToString(reason));
});

// What arrived since the last call, as JSON text, or null; the caller frees it.
EM_JS(char*, drift_js_net_take, (), {
    var net = Module.driftNet;
    if (!net || net.queue.length === 0) return 0;
    var text = JSON.stringify(net.queue);
    net.queue = [];
    return stringToNewUTF8(text);
});
#endif

} // namespace

bool netService(const std::string& f, const json& a, json& out) {
    if (f == "http_request") {
        int32_t id = g_next++;
        std::string headers = a[3].get<std::string>();
        if (headers.empty()) headers = "{}";
#ifdef __EMSCRIPTEN__
        drift_js_http(id, a[0].get<std::string>().c_str(), a[1].get<std::string>().c_str(),
                      a[2].get<std::string>().c_str(), headers.c_str());
#else
        postMessage("HttpResponse", {{"request", {{"id", id}}}, {"status", 0}, {"ok", false}, {"body", ""},
                                     {"error", "HTTP requests need the web build"}});
#endif
        out = id;
        return true;
    }
    if (f == "ws_open") {
        int32_t id = g_next++;
#ifdef __EMSCRIPTEN__
        drift_js_ws_open(id, a[0].get<std::string>().c_str());
#else
        postMessage("SocketEvent", {{"socket", {{"id", id}}}, {"kind", "Error"},
                                    {"text", "WebSockets need the web build"}, {"code", 0}});
        postMessage("SocketEvent", {{"socket", {{"id", id}}}, {"kind", "Closed"}, {"text", ""}, {"code", 1006}});
#endif
        out = id;
        return true;
    }
    if (f == "ws_send") {
#ifdef __EMSCRIPTEN__
        out = drift_js_ws_send(a[0].get<int>(), a[1].get<std::string>().c_str()) != 0;
#else
        out = false;
#endif
        return true;
    }
    if (f == "ws_close") {
#ifdef __EMSCRIPTEN__
        drift_js_ws_close(a[0].get<int>(), 1000, "");
#endif
        out = nullptr;
        return true;
    }
    return false;
}

void netPoll() {
#ifdef __EMSCRIPTEN__
    char* text = drift_js_net_take();
    if (!text) return;
    json events = json::parse(text, nullptr, false);
    std::free(text);
    if (!events.is_array()) return;
    for (auto& e : events) outbox().push_back(std::move(e));
#endif
}

} // namespace drift::candela
