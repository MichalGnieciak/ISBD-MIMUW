#pragma once
#include "metastore/metastore.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace isbd::api {

struct ErrorDto {
    std::string message;
};

struct ProblemItemDto {
    std::string error;
    std::string context;
};

struct MultipleProblemsDto {
    std::vector<ProblemItemDto> problems;

    static MultipleProblemsDto from_validation_problems(
        const std::vector<ValidationProblem>& validation_problems);
};

void to_json(nlohmann::json& j, const ErrorDto& dto);
void to_json(nlohmann::json& j, const ProblemItemDto& dto);
void to_json(nlohmann::json& j, const MultipleProblemsDto& dto);

} // namespace isbd::api