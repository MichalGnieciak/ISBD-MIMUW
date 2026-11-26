#pragma once
#include "api/dto/common_dto.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace isbd::api {

inline void send_json(httplib::Response& res, int status, const nlohmann::json& payload) {
    res.status = status;
    res.set_content(payload.dump(), "application/json");
}

template <typename TDto>
bool try_parse_body(const httplib::Request& req, TDto& out_dto, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error& ex) {
        send_json(res, 400, MultipleProblemsDto{{{std::string("Invalid JSON: ") + ex.what(), ""}}});
        return false;
    }

    std::vector<ProblemItemDto> parse_problems;
    if (!TDto::parse(body, out_dto, parse_problems)) {
        send_json(res, 400, MultipleProblemsDto{std::move(parse_problems)});
        return false;
    }

    return true;
}

template <typename TDto>
bool try_parse_optional_body(const httplib::Request& req, TDto& out_dto, httplib::Response& res) {
    std::string parse_err;
    if (!TDto::parse(req.body, out_dto, parse_err)) {
        send_json(res, 400, ErrorDto{parse_err});
        return false;
    }
    return true;
}

inline bool is_whitespace_only(const std::string& s) {
    for (unsigned char ch : s) {
        if (!std::isspace(ch)) {
            return false;
        }
    }
    return true;
}

} // namespace isbd::api