/*
 * Copyright 2021 Redpanda Data, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */

#pragma once

#include "pandaproxy/json/types.h"
#include "pandaproxy/parsing/exceptions.h"
#include "pandaproxy/parsing/from_chars.h"
#include "reflection/type_traits.h"
#include "vassert.h"

#include <seastar/http/httpd.hh>
#include <seastar/http/request.hh>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <boost/algorithm/string/split.hpp>

namespace pandaproxy::parse {

namespace detail {

namespace ppj = pandaproxy::json;

template<typename T>
T parse_param(std::string_view type, std::string_view key, ss::sstring value) {
    if (!reflection::is_std_optional<T> && value.empty()) {
        throw error(
          error_code::empty_param,
          fmt::format("Missing mandatory {} '{}'", type, key));
    }
    auto res = parse::from_chars<T>{}(value);
    if (res.has_error()) {
        throw error(
          error_code::invalid_param,
          fmt::format(
            "Invalid {} '{}' got '{}'",
            type,
            key,
            replace_control_chars_in_string(value)));
    }
    return res.value();
}

inline ppj::serialization_format parse_serialization_format(
  std::string_view format,
  const std::vector<json::serialization_format>& supported) {
    static constexpr const std::array<std::string_view, 2> none{"", "*/*"};
    std::vector<ss::sstring> formats;
    boost::split(
      formats, format, boost::is_any_of(",; "), boost::token_compress_on);
    if (formats.empty()) {
        formats.emplace_back(none[0]);
    }

    const auto is_none = [](std::string_view format) {
        return std::any_of(
          none.begin(), none.end(), [format](auto n) { return n == format; });
    };

    // Select the first provided requested format that is supported
    // If none is provided, return the first supported
    for (const auto& result : formats) {
        for (auto fmt : supported) {
            if (name(fmt) == result) {
                return fmt;
            } else if (
              fmt == ppj::serialization_format::none && is_none(result)) {
                vassert(
                  !supported.empty(), "Provide at least one supported format");
                return supported[0];
            }
        }
    }
    return json::serialization_format::unsupported;
}

} // namespace detail

template<typename T>
T header(const ss::httpd::request& req, const ss::sstring& name) {
    return detail::parse_param<T>("header", name, req.get_header(name));
}

template<typename T>
T request_param(const ss::httpd::request& req, const ss::sstring& name) {
    const auto& param{req.param[name]};
    ss::sstring value;
    if (!ss::httpd::connection::url_decode(param, value)) {
        throw error(
          error_code::invalid_param,
          fmt::format("Invalid parameter '{}' got '{}'", name, param));
    }
    return detail::parse_param<T>("parameter", name, value);
}

template<typename T>
T query_param(const ss::httpd::request& req, const ss::sstring& name) {
    return detail::parse_param<T>("parameter", name, req.get_query_param(name));
}

inline json::serialization_format accept_header(
  const seastar::httpd::request& req,
  const std::vector<json::serialization_format>& supported) {
    auto accept = req.get_header("Accept");
    auto fmt = detail::parse_serialization_format(accept, supported);
    if (fmt == json::serialization_format::unsupported) {
        throw parse::error(parse::error_code::not_acceptable);
    }
    return fmt;
}

inline json::serialization_format content_type_header(
  const seastar::httpd::request& req,
  const std::vector<json::serialization_format>& supported) {
    auto content_type = req.get_header("Content-Type");
    auto fmt = detail::parse_serialization_format(content_type, supported);
    if (fmt == json::serialization_format::unsupported) {
        throw parse::error(parse::error_code::unsupported_media_type);
    }
    return fmt;
}

} // namespace pandaproxy::parse
