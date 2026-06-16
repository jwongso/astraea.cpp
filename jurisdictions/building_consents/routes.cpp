// Statute routing table for NZ building consents.
// Direct port of buildingconsents/app/jurisdiction.py:_ROUTES.
#include "building_consents/routes.hpp"

namespace astraea::nz_building {

static const std::vector<StatuteRoute> ROUTES = {

    // ---- Structures ----

    {
        .intent      = "carport-exemption",
        .include_any = {
            "carport", "car port", "covered parking",
        },
        .forced_sections = {
            "NZLEG/EBWO2020/s11",
            "NZLEG/EBWO2020/s18A",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "carport exempt building work area height",
    },
    {
        .intent      = "garage",
        .include_any = {
            "garage", "workshop", "fully enclosed vehicle",
        },
        .forced_sections = {
            "NZLEG/BA2004/s41",
            "NZLEG/EBWO2020/s3A",
            "NZLEG/EBWO2020/s3B",
        },
        .synthetic_query = "garage enclosed building consent exempt detached",
    },
    {
        .intent      = "shed-barn",
        .include_any = {
            "shed", "pole shed", "barn", "hay barn", "farm building",
            "man cave", "chicken coop", "summer house", "cabin",
        },
        .forced_sections = {
            "NZLEG/EBWO2020/s4A",
            "NZLEG/EBWO2020/s49",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "pole shed hay barn rural zone exempt building work",
    },
    {
        .intent      = "detached-building-sleepout",
        .include_any = {
            "sleepout", "sleep out", "outbuilding", "kitset", "prefab",
        },
        .forced_sections = {
            "NZLEG/EBWO2020/s3A",
            "NZLEG/EBWO2020/s3B",
            "NZLEG/EBWO2020/s43",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "single-storey detached building sleepout exempt 30 square metres lightweight kitset",
    },
    {
        .intent      = "granny-flat-standalone",
        .include_any = {
            "granny flat", "minor dwelling", "secondary dwelling",
            "standalone dwelling", "small standalone",
        },
        .forced_sections = {
            "NZLEG/BA2004/s41",
            "NZLEG/EBWO2020/s3A",
            "NZLEG/EBWO2020/s3B",
        },
        .synthetic_query = "granny flat standalone dwelling 70 square metres exempt consent single storey",
    },
    {
        .intent      = "deck-porch-veranda",
        .include_any = {
            "deck", "sun deck", "porch", "veranda", "verandah",
            "pergola", "arbour", "platform", "elevated",
        },
        .forced_sections = {
            "NZLEG/EBWO2020/s9",
            "NZLEG/EBWO2020/s17A",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "deck platform porch veranda elevated structure building consent exempt Schedule 1 height above ground area square metres threshold",
    },
    {
        .intent      = "awning",
        .include_any = {
            "awning", "shade sail", "canopy",
        },
        .forced_sections = {
            "NZLEG/EBWO2020/s7",
            "NZLEG/EBWO2020/s8",
            "NZLEG/EBWO2020/s16A",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "awning canopy exempt building work size area",
    },
    {
        .intent      = "enclosed-veranda-conservatory",
        .include_any = {
            "closing in", "enclosed veranda", "enclosed patio",
            "conservatory", "sun room", "sunroom",
        },
        .forced_sections = {
            "NZLEG/BA2004/s41",
            "NZLEG/EBWO2020/s9",
        },
        .synthetic_query = "closing in veranda patio conservatory enclosure building consent",
    },

    // ---- Swimming pools ----

    {
        .intent      = "swimming-pool",
        .include_any = {
            "swimming pool", "pool", "spa pool", "spa",
            "hot tub", "paddling pool",
        },
        .forced_sections = {
            "NZLEG/BA2004/s23",
            "NZLEG/BA2004/s162C",
            "NZLEG/BA2004/s162D",
            "NZLEG/BA2004/s21A",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "swimming pool building consent fencing access restriction residential pool",
    },

    // ---- Energy and heating ----

    {
        .intent      = "solar-panels",
        .include_any = {
            "solar panel", "solar cell", "photovoltaic", "pv panel",
            "solar array", "rooftop solar", "ground-mounted solar",
        },
        .forced_sections = {
            "NZLEG/BA2004/s28C",
            "NZLEG/EBWO2020/s48",
            "NZLEG/BA2004/s48",
            "NZLEG/BA2004/s48A",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "solar panel array ground-mounted roof-mounted exempt building work consent area",
    },
    {
        .intent      = "water-heater",
        .include_any = {
            "water heater", "hot water cylinder", "hot water tank",
            "wetback", "continuous hot water", "instant hot water", "gas water",
        },
        .forced_sections = {
            "NZLEG/BA2004/s36",
            "NZLEG/BA2004/s38",
            "NZLEG/BA2004/s35",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "water heater replacement repair exempt plumber building consent",
    },
    {
        .intent      = "outdoor-fireplace",
        .include_any = {
            "outdoor fireplace", "pizza oven", "bbq", "barbeque",
            "fire pit", "outdoor oven", "permanent fireplace",
        },
        .forced_sections = {
            "NZLEG/EBWO2020/s28A",
            "NZLEG/BA2004/s28A",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "permanent outdoor fireplace oven barbecue exempt building work",
    },

    // ---- Plumbing and drainage ----

    {
        .intent      = "plumbing-drainage",
        .include_any = {
            "plumbing", "drain", "drainage", "sanitary", "toilet",
            "sink", "shower", "waste pipe", "grey water", "gully trap",
        },
        .forced_sections = {
            "NZLEG/BA2004/s35",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "plumbing drainage sanitary alteration exempt authorised drainlayer building consent",
    },

    // ---- Ground and subfloor ----

    {
        .intent      = "ground-moisture-barrier",
        .include_any = {
            "ground moisture", "moisture barrier", "polythene",
            "underfloor", "vapour barrier", "plastic sheet",
        },
        .forced_sections = {
            "NZLEG/BA2004/s13A",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "ground moisture barrier underfloor polythene exempt building work",
    },

    // ---- Interior alterations ----

    {
        .intent      = "interior-alterations",
        .include_any = {
            "internal wall", "load-bearing wall", "load bearing",
            "structural wall", "bracing", "doorway", "interior alteration",
            "non-residential",
        },
        .forced_sections = {
            "NZLEG/BA2004/s10",
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "interior alteration internal wall load-bearing structural building consent",
    },

    // ---- Consent process ----

    {
        .intent      = "certificate-of-acceptance",
        .include_any = {
            "certificate of acceptance", "unconsented", "without consent",
            "retrospective consent", "urgent work", "notice to fix",
        },
        .forced_sections = {
            "NZLEG/BA2004/s96",
            "NZLEG/BA2004/s97",
            "NZLEG/BA2004/s98",
            "NZLEG/BA2004/s99",
        },
        .synthetic_query = "certificate of acceptance unconsented building work territorial authority application",
    },
    {
        .intent      = "schedule-1-exempt-overview",
        .include_any = {
            "schedule 1", "exempt work", "exempt building", "exemption",
        },
        .forced_sections = {
            "NZLEG/BA2004/s41",
        },
        .synthetic_query = "schedule 1 exempt building work building act 2004",
    },
};

std::span<const StatuteRoute> get_routes() {
    return ROUTES;
}

} // namespace astraea::nz_building
