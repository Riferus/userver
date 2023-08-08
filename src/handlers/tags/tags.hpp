#pragma once

#include "userver/components/component_list.hpp"
#include "userver/components/component.hpp"
#include "userver/formats/json/serialize_container.hpp"
#include "userver/server/handlers/http_handler_json_base.hpp"
#include "userver/storages/postgres/cluster.hpp"
#include "userver/storages/postgres/component.hpp"

namespace real_medium::handlers::tags::get {

class Handler final : public userver::server::handlers::HttpHandlerJsonBase {
 public:
  static constexpr std::string_view kName = "handler-get-tags";

  Handler(const userver::components::ComponentConfig& config,
       const userver::components::ComponentContext& component_context);

  userver::formats::json::Value HandleRequestJsonThrow(
      const userver::server::http::HttpRequest&,
      const userver::formats::json::Value&,
      userver::server::request::RequestContext&) const override;

 private:
  userver::storages::postgres::ClusterPtr pg_cluster_;
};

}  // namespace real_medium::handlers::tags::get