#include "common_dto.h"

namespace isbd::api {

MultipleProblemsDto MultipleProblemsDto::from_validation_problems(
    const std::vector<ValidationProblem>& validation_problems) {
    MultipleProblemsDto dto;
    dto.problems.reserve(validation_problems.size());
    for (const auto& vp : validation_problems) {
        dto.problems.push_back({vp.error, vp.context});
    }
    return dto;
}

void to_json(nlohmann::json& j, const ErrorDto& dto) {
    j = nlohmann::json{{"message", dto.message}};
}

void to_json(nlohmann::json& j, const ProblemItemDto& dto) {
    j = nlohmann::json{{"error", dto.error}};
    if (!dto.context.empty()) {
        j["context"] = dto.context;
    }
}

void to_json(nlohmann::json& j, const MultipleProblemsDto& dto) {
    j = nlohmann::json{{"problems", dto.problems}};
}

} // namespace isbd::api