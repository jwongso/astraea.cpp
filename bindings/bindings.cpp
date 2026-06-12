#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "astraea/routing.hpp"
#include "astraea/sanitize.hpp"

namespace py = pybind11;
using namespace astraea;

PYBIND11_MODULE(_astraea_cpp, m) {
    m.doc() = "Astraea C++ core bindings for differential testing against the Python reference.";

    // SanitizeError translates to a Python exception on the way out of any binding.
    static py::exception<SanitizeError> exc_sanitize(m, "SanitizeError", PyExc_RuntimeError);
    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p) std::rethrow_exception(p);
        } catch (const SanitizeError& e) {
            py::set_error(exc_sanitize, e.what());
        }
    });

    // StatuteRoute — writable so pytest can build route tables from Python dataclasses.
    py::class_<StatuteRoute>(m, "StatuteRoute")
        .def(py::init<>())
        .def_readwrite("intent",               &StatuteRoute::intent)
        .def_readwrite("include_any_precise",  &StatuteRoute::include_any_precise)
        .def_readwrite("include_any_broad",    &StatuteRoute::include_any_broad)
        .def_readwrite("require_context_any",  &StatuteRoute::require_context_any)
        .def_readwrite("include_any",          &StatuteRoute::include_any)
        .def_readwrite("include_all",          &StatuteRoute::include_all)
        .def_readwrite("exclude_any",          &StatuteRoute::exclude_any)
        .def_readwrite("forced_sections",      &StatuteRoute::forced_sections)
        .def_readwrite("leg_allow_list",       &StatuteRoute::leg_allow_list)
        .def_readwrite("guidance_sources",     &StatuteRoute::guidance_sources)
        .def_readwrite("synthetic_query",      &StatuteRoute::synthetic_query)
        .def_readwrite("case_synthetic_query", &StatuteRoute::case_synthetic_query)
        .def_readwrite("priority",             &StatuteRoute::priority)
        .def_readwrite("notes",                &StatuteRoute::notes);

    py::class_<TriggerPath>(m, "TriggerPath")
        .def_readonly("intent", &TriggerPath::intent)
        .def_readonly("path",   &TriggerPath::path);

    py::class_<NearMiss>(m, "NearMiss")
        .def_readonly("intent",        &NearMiss::intent)
        .def_readonly("broad_matched", &NearMiss::broad_matched);

    py::class_<IgnoredRoute>(m, "IgnoredRoute")
        .def_readonly("intent", &IgnoredRoute::intent)
        .def_readonly("reason", &IgnoredRoute::reason);

    py::class_<RouteDecision>(m, "RouteDecision")
        .def_readonly("triggered",              &RouteDecision::triggered)
        .def_readonly("matched_intents",        &RouteDecision::matched_intents)
        .def_readonly("trigger_terms",          &RouteDecision::trigger_terms)
        .def_readonly("trigger_paths",          &RouteDecision::trigger_paths)
        .def_readonly("forced_sections",        &RouteDecision::forced_sections)
        .def_readonly("leg_allow_list",         &RouteDecision::leg_allow_list)
        .def_property_readonly("boosted_act_ids",
            [](const RouteDecision& d) -> std::vector<std::string> {
                return {d.boosted_act_ids.begin(), d.boosted_act_ids.end()};
            })
        .def_readonly("leg_synthetic_queries",  &RouteDecision::leg_synthetic_queries)
        .def_readonly("case_synthetic_queries", &RouteDecision::case_synthetic_queries)
        .def_readonly("dominant_route",         &RouteDecision::dominant_route)
        .def_readonly("dominance_reason",       &RouteDecision::dominance_reason)
        .def_readonly("ignored_routes",         &RouteDecision::ignored_routes)
        .def_readonly("near_miss_routes",       &RouteDecision::near_miss_routes);

    m.def("sanitize_question",
        [](const std::string& text, int max_chars) -> std::string {
            return sanitize_question(text, max_chars);
        },
        py::arg("text"), py::arg("max_chars") = 1200);

    m.def("normalize_query",
        [](const std::string& text) -> std::string {
            return normalize_query(text);
        },
        py::arg("text"));

    m.def("build_route_decision",
        [](const std::string& original, const std::string& rewritten,
           const std::vector<StatuteRoute>& routes) -> RouteDecision {
            return build_route_decision(original, rewritten, routes);
        },
        py::arg("original"), py::arg("rewritten"), py::arg("routes"));
}
