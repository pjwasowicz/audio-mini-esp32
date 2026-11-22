#include "Panel.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "BellHTTPServer.h"
#include "Dziennik.h"
#include "Sterownik.h"
#include "civetweb.h"
#include "nlohmann/json.hpp"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

namespace panel {

using Odp = std::unique_ptr<bell::BellHTTPServer::HTTPResponse>;

static Odp html(const char* tresc, size_t n) {
  auto r = std::make_unique<bell::BellHTTPServer::HTTPResponse>();
  r->body = (uint8_t*)malloc(n);
  memcpy(r->body, tresc, n);
  r->bodySize = n;
  r->headers["Content-Type"] = "text/html; charset=utf-8";
  r->headers["Cache-Control"] = "no-cache";
  r->status = 200;
  return r;
}

static nlohmann::json cialo(struct mg_connection* conn) {
  std::string body;
  auto info = mg_get_request_info(conn);
  if (info->content_length > 0 && info->content_length < 4096) {
    body.resize(info->content_length);
    size_t got = 0;
    while (got < body.size()) {
      int n = mg_read(conn, body.data() + got, body.size() - got);
      if (n <= 0) break;
      got += n;
    }
    body.resize(got);
  }
  return nlohmann::json::parse(body, nullptr, false);
}

void rejestruj(std::shared_ptr<bell::BellHTTPServer> s) {
  std::weak_ptr<bell::BellHTTPServer> w = s;
  auto strona = [](struct mg_connection*) { return html(index_html_start, index_html_end - index_html_start); };
  s->registerGet("/", strona);
  s->registerGet("/index.html", strona);
  s->registerGet("/api/status", [w](struct mg_connection*) { return w.lock()->makeJsonResponse(sterownik::json_status()); });
  s->registerGet("/api/wejscie", [w](struct mg_connection*) { return w.lock()->makeJsonResponse(sterownik::json_wejscie()); });
  s->registerGet("/api/system", [w](struct mg_connection*) { return w.lock()->makeJsonResponse(sterownik::json_system()); });
  s->registerGet("/api/resety", [w](struct mg_connection*) { return w.lock()->makeJsonResponse(dziennik::json()); });

  s->registerPost("/api/wejscie", [w](struct mg_connection* c) {
    auto j = cialo(c);
    if (!j.is_object() || !j.contains("wejscie") || !j["wejscie"].is_number_integer())
      return w.lock()->makeJsonResponse("{\"blad\":\"oczekiwane {\\\"wejscie\\\": n}\"}", 400);
    sterownik::ustaw_wejscie(j["wejscie"].get<int>());
    return w.lock()->makeJsonResponse("{\"ok\":true}");
  });
  s->registerGet("/api/strojenie", [w](struct mg_connection*) { return w.lock()->makeJsonResponse(sterownik::json_strojenie()); });
  s->registerPost("/api/strojenie", [w](struct mg_connection* c) {
    auto j = cialo(c);
    if (!j.is_object()) return w.lock()->makeJsonResponse("{\"blad\":\"oczekiwane {\\\"kolano\\\": n}\"}", 400);
    if (j.contains("kolano") && j["kolano"].is_number()) sterownik::ustaw_kolano(j["kolano"].get<int>());
    return w.lock()->makeJsonResponse(sterownik::json_strojenie());
  });
  s->registerPost("/api/glosnosc", [w](struct mg_connection* c) {
    auto j = cialo(c);
    if (!j.is_object() || !j.contains("glosnosc") || !j["glosnosc"].is_number())
      return w.lock()->makeJsonResponse("{\"blad\":\"oczekiwane {\\\"glosnosc\\\": 0-100}\"}", 400);
    sterownik::ustaw_glosnosc(j["glosnosc"].get<int>());
    return w.lock()->makeJsonResponse("{\"ok\":true}");
  });
  for (const char* p : {"playpause", "play", "pause", "next", "prev"}) {
    std::string polecenie = p;
    s->registerPost("/api/player/" + polecenie, [w, polecenie](struct mg_connection*) {
      sterownik::odtwarzanie(polecenie);
      return w.lock()->makeJsonResponse("{\"ok\":true}");
    });
  }
}

}  // namespace panel
