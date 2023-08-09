
#pragma once

#include <fmt/format.h>
#include <string>
#include <string_view>

#include "userver/server/handlers/http_handler_base.hpp"

#include "userver/storages/postgres/cluster.hpp"
#include "userver/storages/postgres/component.hpp"

#include "userver/components/component_config.hpp"
#include "userver/components/component_context.hpp"

#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>



namespace real_medium::handlers::users::put {

class Handler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-user-put";
  
  Handler(const userver::components::ComponentConfig& config,
        const userver::components::ComponentContext& component_context);
  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest& request,
      userver::server::request::RequestContext&) const override;
  using HttpHandlerBase::HttpHandlerBase;
 private:
  userver::storages::postgres::ClusterPtr pg_cluster_;
};



} // namespace real_medium::handlers::users::put
