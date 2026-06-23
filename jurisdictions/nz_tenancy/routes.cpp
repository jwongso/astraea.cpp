// Statute routing table for NZ residential tenancy law (RTA 1986).
// Direct port of jurisdictions/nz_tenancy/routes.py - field order and values
// must remain identical so the C++/Python differential parity tests stay green.
#include "nz_tenancy/routes.hpp"

namespace astraea::nz_tenancy {

static const std::vector<StatuteRoute> ROUTES = {

    // ── PROPERTY CONDITION ────────────────────────────────────────────────────

    {
        .intent = "wear_and_tear",
        .include_any = {
            "fair wear and tear", "wear and tear", "normal wear",
            "tenant damage", "damage claim", "repair cost",
            "landlord charge", "liable for damage", "damage to the",
            "worn", "deteriorated", "deterioration",
            "carpet damage", "carpet replacement", "carpet clean", "carpet wear",
            "bond deduction", "deducting from", "deduct from bond",
            "withholding my bond", "withheld my bond", "withhold my bond",
            "insurance excess", "carpet stain", "stain on carpet",
            "marks on carpet", "paint mark", "paint patch",
            "crack", "cracked", "cracking", "cracks", "broken mirror",
            "clean on move out", "how clean for move out", "cleanliness at end",
            "vacating",
            "dented", "dent in", "accidental damage", "accident damage",
            "threadbare", "thread bare", "worn out carpet", "worn carpet",
            "garden tools", "tools left", "left tools",
            "clean and tidy", "rubbish left behind", "left rubbish", "rubbish we left",
            "steam clean", "steam cleaning", "steam cleaned",
            "carpet shampoo", "shampoo the carpet", "shampoo carpets", "shampooed",
            "professional clean", "end of tenancy clean", "required to clean",
            "wind damage", "storm damage", "hinge broke", "hinge broken",
            "before i exit", "before i move out", "before we move out",
            "empty the bins", "clean the bins", "bins before leaving",
            "bins at move out", "rubbish bins", "bins need to be",
            "what to do with rubbish", "rubbish before i leave",
            "meth test", "meth testing", "contamination test", "contamination",
            "withhold the quote", "withheld the quote", "won't provide quote",
            "quote for repairs", "see the quote", "quote withheld", "provide the quote",
            "replace the carpet", "replace carpet", "full carpet replacement", "new carpet",
            "breach notice", "breached for", "issued a breach", "breach of tenancy notice",
            "lawn clippings", "lawn maintenance", "lawn mowing", "mowing the lawn",
            "mowed the lawn", "mow the lawn", "did the lawns", "lawn was mowed",
            "paying for an accident", "pay for an accident", "accident to the",
            "garage door", "garage door damage", "door motor",
            "pin hole", "pin holes", "wallpaper", "wallpaper damage", "wallpaper torn",
            "wall hole", "wall holes",
            "no quotes provided", "no invoice provided", "no invoices provided",
            "no quotes or invoices", "estimated damage", "minor damages",
            "blu-tack damage", "blue tack damage", "picture holes",
            "repaint", "repainting", "re-paint", "re-painting",
            "repaint the wall", "repainted the wall", "paint the wall",
            "sticky hooks", "command hooks", "hooks on the wall", "adhesive hooks",
            "commercial cleaners", "before and after photos", "cleaning photos",
            "is this reasonable", "is the cost reasonable", "is the quote reasonable",
            "reasonable cost to", "reasonable amount to", "reasonable charge",
            "overcharging me for", "overcharging for repairs", "too expensive for repairs",
            "breached the contract", "breach of contract",
            "reasonably tidy", "keep the house tidy", "house reasonably tidy",
            "tidiness breach", "not tidy enough", "tidiness standard",
            "s49a", "s49b",
            "scuff marks", "scuff mark", "black scuff", "marks off walls", "scuff off",
            "water marks", "watermarks", "water stains", "water stain",
            "bond deducted", "deducted from my bond", "deducted from the bond",
            "deducted from bond",
            "chattels list", "chattel list", "chattel",
            "empty and clean the bins", "bins need to be empty", "empty the bins before",
            "bins emptied before", "bins clean before",
            "soft in lots of places", "soft in many places", "floor is soft in",
        },
        .exclude_any = {
            // Contamination FROM A NEIGHBOUR is handled by neighbour_contamination,
            // not wear_and_tear - exclude neighbour-context triggers to avoid overcapture
            "from the neighbour", "from next door", "from a neighbour",
            "cooking meth next door", "meth next door", "neighbour cooking meth",
            "contamination from neighbour", "contamination from next door",
            "neighbour contamination", "next door contamination",
        },
        .forced_sections = {"NZLEG/RTA/s49A", "NZLEG/RTA/s49B", "NZLEG/RTA/s40", "NZLEG/RTA/s45"},
        .leg_allow_list = {
            "NZLEG/RTA/s49A",
            "NZLEG/RTA/s49B",
            "NZLEG/RTA/s40",
            "NZLEG/RTA/s42A",
            "NZLEG/RTA/s42B",
            "NZLEG/RTA/s18AA",
            "NZLEG/RTA/s42C",
            "NZLEG/RTA/s45",
        },
        .synthetic_query =
            "tenant not liable fair wear tear exception section 49A damage "
            "landlord cannot charge deterioration reasonable use natural forces "
            "residential tenancies act",
        .notes = "Tenant damage liability and fair wear and tear exception.",
        .rule_card =
            "Fair wear and tear and damage liability (RTA s40):\n"
            "WHAT IS FAIR WEAR AND TEAR:\n"
            "- Fair wear and tear means deterioration from normal everyday use, "
            "natural aging, and the presence of children. The tenant is NOT liable "
            "for this. Specific examples: carpet worn flat in traffic areas, "
            "net curtains yellowed by sun, wallpaper fading or minor peeling, "
            "paint scuffs from normal furniture use, minor marks on walls.\n"
            "AGE AND TENANCY DURATION:\n"
            "- The LONGER the tenancy, the MORE wear is expected. After 5 or more "
            "years, items like carpet, net curtains, and wallpaper will naturally "
            "show significant wear that is ENTIRELY fair wear and tear. Do NOT suggest "
            "the tenant is responsible for replacing items that have simply aged.\n"
            "- Carpet age matters: if the carpet was already old or worn at move-in, "
            "replacement costs must be prorated for age. A carpet already at "
            "end-of-life CANNOT be fully charged to the tenant.\n"
            "PROOF OF CARE:\n"
            "- A tenant who has proactively cleaned (e.g., had carpets professionally "
            "cleaned twice, kept receipts, followed up in writing) demonstrates good "
            "faith and strongly supports a fair wear and tear position if the landlord "
            "disputes deductions. Reference this if the tenant mentions cleaning records.\n"
            "EVIDENCE BURDEN:\n"
            "- The LANDLORD must prove the damage exceeds fair wear and tear AND was "
            "caused by the tenant. Estimates without quotes or invoices are "
            "insufficient. The tenant is entitled to see the evidence before agreeing.\n"
            "IF QUERY MENTIONS A MAINTENANCE PROBLEM (water pressure, heating, etc):\n"
            "- Also advise the tenant to notify the landlord IN WRITING immediately "
            "about the maintenance issue and request repair. This is the landlord's "
            "responsibility under s45. Document date sent and method.\n"
            "IF QUERY MENTIONS A PET CLAUSE IN THE AGREEMENT:\n"
            "- Any clause requiring professional carpet cleaning at end of tenancy "
            "because of a pet is VOID under RTA s11. Such clauses are unenforceable. "
            "The standard is the same as any tenant: leave the property reasonably "
            "clean and tidy. Do NOT suggest the tenant should comply with a void clause. "
            "Only actual damage or genuine uncleanliness beyond normal use creates liability.",
    },

    {
        .intent = "property_change",
        .include_any_precise = {
            "alteration", "altered",
            // "alter" removed: it is a substring of "alternative", causing false matches
            // on "alternative heating options" and similar queries.
            "minor change",
            "renovate", "renovation",
            "without consent", "without permission",
            "landlord consent", "written consent", "written permission",
            "landlord permission",
            "planted trees", "planted a tree", "planted several",
            "plant trees", "plant a tree",
            "drawing pin", "drawing pins", "command strip", "command strips",
            "picture hook", "picture hooks", "blu-tack", "blu tack", "bluetack",
            "security camera", "surveillance camera", "cctv",
            "doorbell camera", "camera outside", "camera facing",
            "adhesive", "renter friendly", "renter-friendly",
            "anchor furniture", "anchoring furniture", "bolt furniture",
            "bolt to wall", "fix to wall", "furniture to wall", "furniture anchoring",
            "padlock the gate", "padlock my gate", "padlock the front gate",
            "padlock the door", "add a padlock",
        },
        .include_any_broad = {
            "plant", "planted", "tree", "trees", "shrub", "hedge",
            "garden", "backyard", "back yard", "lawn",
            "fence",
            "fixture",
            "install", "installed", "installs", "installing",
            "improvement",
        },
        .require_context_any = {
            "consent", "permission", "consented",
            "without consent", "without permission",
            "landlord consent", "landlord's consent",
            "written consent", "written permission",
            "landlord permission", "landlord's permission",
            "alteration", "minor change", "improvement",
        },
        .exclude_any = {
            "healthy homes", "building code", "building act",
            "resource management act",
            "plumbing", "sewage", "shower drain",
            "landlord failed to maintain", "landlord hasn't maintained",
            "landlord has not maintained",
            "not repaired", "not fixed", "won't fix", "wont fix",
            "hasn't fixed", "hasnt fixed",
            "broken", "not working",
            "entered my home", "came into my home", "already in my home",
            "was in my home", "was in my house", "was in my flat",
            "home owner", "homeowner", "property owner",
            "uninvited", "is she allowed", "is he allowed",
            // Prevent "alternative heating" / heating-related queries from matching
            // via the "alter" substring. These are healthy_homes or repair questions.
            "alternative heating", "heating options", "heating alternative",
            "diesel heating", "heat pump", "heater",
        },
        .forced_sections = {"NZLEG/RTA/s40", "NZLEG/RTA/s42A", "NZLEG/RTA/s42B"},
        .leg_allow_list = {"NZLEG/RTA/s40", "NZLEG/RTA/s42A", "NZLEG/RTA/s42B"},
        .synthetic_query =
            "tenant obligations alter improve add fixtures to land garden "
            "written consent landlord section 40 42A 42B residential tenancies act",
        .priority = 10,
        .notes = "Tenant changes to premises, garden, land, fixtures.",
        .rule_card =
            "Property changes and reasonable use (RTA s40, s42A, s42B):\n"
            "DRAWING PINS, PICTURE HOOKS, AND MINOR FASTENERS:\n"
            "- Drawing pins, small picture hooks, and minor adhesive strips used for "
            "normal picture-hanging are REASONABLE USE of the home - they are NOT "
            "'alterations' requiring landlord consent under s42A/s42B.\n"
            "- The pin-prick or small screw holes left by normal picture hanging are "
            "minor and easily patched/painted. They constitute FAIR WEAR AND TEAR "
            "under s40, NOT tenant damage the landlord can charge for.\n"
            "- A PM or landlord cannot enforce an ABSOLUTE ban on drawing pins or "
            "normal picture hanging, as this would deny the tenant the reasonable "
            "enjoyment of their home. Tenants have an implied right to reasonable use.\n"
            "- However, if the tenancy agreement contains a SPECIFIC, REASONABLE "
            "restriction (e.g., 'no nails in plasterboard' or 'use only adhesive "
            "strips') and the clause allows reasonable use within those limits, it "
            "may be enforceable.\n"
            "- Tenant response to an absolute ban: reference s40 (fair wear and tear), "
            "note that minor pin holes from normal picture hanging are not damage, and "
            "request the specific clause in the tenancy agreement that restricts this.\n"
            "ALTERATIONS REQUIRING CONSENT (s42A, s42B):\n"
            "- PERMANENT alterations (drilling for shelves, installing fixtures, painting, "
            "structural changes) DO require landlord consent under s42A.\n"
            "- Landlord must not unreasonably withhold consent for minor improvements "
            "that are easily reversed (s42B).\n"
            "What NOT to say:\n"
            "- Do NOT say tenants are not obliged to follow any house rules - reasonable "
            "agreement terms CAN be enforced, as long as they allow reasonable enjoyment.\n"
            "- Do NOT say drawing pins require landlord consent - they do not.",
    },

    {
        .intent = "repairs_tenant_not_at_fault",
        .require_context_any = {
            "landlord", "property manager", "pm",
            "tenant", "rental", "tenancy",
            "heater", "heat pump", "appliance", "chattel",
            "stove", "oven", "hot water", "washing machine",
            "garage door", "lock", "locks", "locked", "locking", "fridge", "dishwasher",
        },
        .include_any = {
            "repair", "repairs", "repaired", "repairing",
            "fix", "fixed", "fixing", "fixes",
            "broken", "not because of me", "not my fault",
            "landlord wants money", "pay for repair", "charged me for repair",
            "heater broken", "heat pump broken", "stove broken", "oven broken",
            "hot water broken", "appliance broken",
            "who pays for", "who is responsible for", "who pays to fix",
            "landlord charging me", "landlord wants me to pay",
            "landlord is asking me to pay", "sent me a bill", "sent a bill",
            "want me to pay for", "responsible for paying",
            "not caused by me", "i didn't break", "i didn't cause",
            "didn't damage", "not broken by me", "broke on its own",
            "stopped working on its own", "failed on its own",
            "equipment failure", "appliance failure",
        },
        .forced_sections = {
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s40",
            "NZLEG/RTA/s78",
            "NZLEG/RTA/s49A",
            "NZLEG/RTA/s109",
        },
        .leg_allow_list = {
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s40",
            "NZLEG/RTA/s56",
            "NZLEG/RTA/s78",
            "NZLEG/RTA/s49A",
            "NZLEG/RTA/s49B",
            "NZLEG/RTA/s109",
            "NZLEG/RTA/s38",
            "NZLEG/RTA/s48",
            "NZLEG/RTA/s77",
            "NZLEG/RTA/s86",
        },
        .guidance_sources = {"MANUAL/damage-and-repairs"},
        .synthetic_query =
            "landlord repair obligation tenant not responsible fair wear and tear "
            "tenant did not cause damage heater appliance broken repair cost "
            "Residential Tenancies Act section 45 section 40 "
            "s78 tribunal work order s49A tenant not liable s49B insurance excess "
            "s109 limitation period s56 notice to remedy s77 tribunal jurisdiction",
        .priority = 8,
        .notes = "Tenant says item broke without tenant fault; landlord seeks repair cost.",
    },

    {
        .intent = "repairs_maintenance",
        .include_any = {
            "not working", "doesn't work", "doesnt work", "isn't working", "isnt working",
            "broken", "won't fix", "wont fix",
            "shower seal", "shower sealed", "shower not sealed", "never been sealed",
            "never sealed", "not sealed properly", "improperly sealed",
            "shower leaking", "shower leak", "shower sealing",
            "hasn't fixed", "hasnt fixed", "not fixed", "not repaired",
            "not maintained", "hasn't maintained", "has not maintained",
            "repair request", "maintenance",
            "hot water", "no hot water", "heating", "no heating",
            "mould", "mold", "damp", "dampness", "moisture", "mildew",
            "condensation", "water damage", "humid", "fungal",
            "flooded", "flooding", "house flooded", "property flooded",
            "leak", "leaks", "leaking", "leaked", "dripping",
            "weathertight", "habitable", "uninhabitable",
            "appliance", "oven", "stove", "fridge",
            "landlord obligation", "landlord's obligation",
            "s45",
            "pest", "pests", "pest control", "infestation", "infested",
            "spider", "spiders", "rats", "mice", "mouse",
            "cockroach", "cockroaches", "ant infestation", "fleas", "bedbugs",
            "bug", "bugs", "insect", "insects",
            "exterminator", "fumigation", "fumigated", "bitten",
            "wasp", "wasps", "wasp nest", "wasp nests", "bees", "beehive",
            "dishwasher", "washing machine", "dryer",
            "overgrown", "state of cleanliness", "clean on move in",
            "clean when i moved", "reasonably clean", "not clean", "wasn't clean",
            "wasn't cleaned", "was not cleaned", "pet hair", "undisclosed pet",
            "water tank", "tank dirty", "tank cleaning", "clean the tank",
            "flush", "flushing", "toilet flush", "flush issue",
            "heat pump", "heatpump",
            "plumbing", "blocked drain", "drain blocked", "drain issue",
            "tree roots", "roots in pipe", "roots in plumbing",
            "fallen tree", "fallen branch", "tree fallen", "branch fallen",
            "big tree", "large tree", "trees near", "tree near power",
            "cut down a tree", "cut down the tree", "tree close to",
            "tree near the lines", "tree near lines", "trim the tree",
            "tree overhanging", "branches overhanging", "tree touching",
            "vinyl flooring", "vinyl floor", "flooring coming up", "floor coming up",
            "flooring unattached", "flooring lifting", "floor lifting",
            "fence damaged", "fence damage", "fence repair", "fence broken",
            "fencing", "broken fence", "storm damage", "damage in the storm",
            "damaged in the storm", "storm damaged",
            "letterbox", "mailbox", "letter box",
            "air con", "air conditioning", "aircon maintenance", "clean the air con",
            "service the air con", "air conditioner",
            "washing the outside", "wash the outside", "clean the outside", "exterior cleaning",
            "outside of the house", "wash the house", "outside washing",
            "animal in ceiling", "possum", "something in ceiling",
            "movement in ceiling", "noise in ceiling", "creature in ceiling",
            "guttering", "gutter", "gutters", "blocked gutter",
            "drainage", "puddle at", "water pooling",
            "bad smell", "horrible smell", "sewage smell", "drain smell",
            "smell from pipe", "smell from drain", "water pipe", "pipes",
            "hob", "gas hob", "oven hob", "stove hob",
            "snapped", "knob snapped", "knob broke", "knob broken",
            "heat lamp", "led light", "led lights", "downlight", "downlights",
            "light fitting", "light bulb", "replace the light", "light not working",
            "window latch", "window latches", "window lock", "window locks",
            "windows don't shut", "windows won't close", "windows won't shut",
            "window won't close", "window won't shut", "window doesn't close",
            "no latch on", "no latch for", "child safe window", "toddler safe",
            "locksmith", "lock broke", "lock broken", "lock failure", "lock stopped working",
            "batteries", "replace the batteries", "who replaces batteries", "smoke alarm battery",
            "railing", "railings", "balustrade", "balustrades", "mezzanine",
            "gap between rails", "gap between railings", "safety rail",
            "tap broke", "tap broken", "tap stopped", "tap not working",
            "sliding door", "door stiff", "stiff door",
            "power disconnected", "power off", "power cut off", "power cut", "power outage",
            "reconnect power", "electricity disconnected", "power has been disconnected",
            "faulty wiring", "faulty circuit", "electrical fault",
            "fire hazard", "fire risk", "almost caught fire",
            "bathroom blocked", "bathroom is blocked", "toilet blocked", "sink blocked",
            "shower blocked", "shower drain", "shower not draining", "bath blocked",
            "time limit for repairs", "how long to fix", "timeframe for repairs",
            "how long should repairs take", "how long does the landlord have to fix",
            "when should repairs be done", "when must repairs be done",
            "how long to respond to repairs", "repair response time",
            "soft floor", "spongy floor", "soft flooring", "floor feels soft",
            "floor is soft", "floors are soft", "soft in places",
            "raised concerns about", "raised a concern", "no response from pm",
            "heard nothing from pm", "pm hasn't responded", "pm not responding",
            "reported the issue", "notified pm about",
            "water filter", "filter cartridge", "under sink filter",
            "filter replacement", "water filtration",
            "get the work completed", "get the work done", "work completed timely",
            "completed in a timely", "get repairs done", "push for repairs",
            "pre-existing", "pre existing damage", "pre-existing hole",
            "reported previously", "already reported", "reported before moving in",
            "hole was pre-existing", "damage was pre-existing",
            "s33",
            "bore pump", "bore water", "farm bore", "water bore",
            "no water for stock", "stock water", "water for horses",
            "farm with a bore", "bore not working", "bore has failed",
            "water heater", "water heater issue", "hot water heater",
            "state of the property", "condition of the property",
            "not happy with the state", "unhappy with the state",
            "liability of landlord", "landlord liability", "indemnify the tenant",
            "indemnify the tenants",
            "code of compliance", "no code of compliance", "CCC",
            "illegal dwelling", "illegal rental",
            "lingering smell", "lingering odour", "curry smell", "odour in",
            "fishy smell", "fishy odour", "fishy odor", "fishy when",
            "smell from heat pump", "smell from the heat pump",
            "odour from heat pump", "odor from heat pump",
            "heat pump smell", "heat pump smells", "heat pump odour",
            "burning smell from", "electrical smell", "electrical odour",
            "strange smell from appliance", "smell from the appliance",
            "smells when i use", "smells when we use", "smells when using",
            "responsible for the garden", "responsible for gardens",
            "garden maintenance", "maintain the garden", "who looks after the garden",
            "trees and shrubs", "hedging in the rental", "garden upkeep",
            "sewage system", "septic tank", "septic system",
            "kitchen sink", "sinks not draining", "sinks aren't draining", "sink not draining",
            "not my fault", "not my problem", "wasn't my fault", "wasn't caused by me",
            "not because of me", "not caused by me", "i didn't break", "i didn't cause",
            "i didn't damage", "didn't damage", "didn't break it", "didn't cause it",
            "not broken by me", "no fault of mine", "fault of the tenant",
            "stopped working on its own", "failed on its own", "broke on its own",
            "equipment failure", "appliance failure", "stopped working by itself",
            "just stopped working", "stopped working without", "broke without",
            "landlord charging me", "landlord wants me to pay", "landlord is asking me to pay",
            "landlord billing me", "landlord send me a bill", "sent me a bill",
            "they want me to pay for", "want me to pay for repairs",
            "responsible for paying", "who pays for repairs", "who is responsible for repairs",
            "who pays to fix", "who pays to repair",
        },
        .exclude_any = {
            "fair wear and tear", "wear and tear",
            "install fixture", "installed fixture",
            "minor change", "minor improvement",
            "plant trees", "planted trees", "planting trees",
            "alteration", "altered",
            "renovation without consent", "renovate without",
            "healthy homes",
        },
        .forced_sections = {"NZLEG/RTA/s45"},
        .leg_allow_list = {
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s56",
            "NZLEG/RTA/s78",
            "NZLEG/RTA/s40",
            "NZLEG/RTA/s47",
        },
        .synthetic_query =
            "landlord responsibility maintain premises reasonable state repair "
            "section 45 habitable condition heating hot water weathertight "
            "residential tenancies act tenant remedies maintenance obligations "
            "equipment broke naturally not caused by tenant landlord must fix "
            "heater shower kitchen appliance stopped working tenant not liable "
            "s78 tribunal work order compensation s56 notice to remedy",
        .notes = "Landlord maintenance and repair obligations (s45).",
        .rule_card =
            "Landlord repair obligations - RTA s45:\n"
            "APPLIANCE SAFETY (fishy, burning, or electrical smell):\n"
            "- A strong fishy, burning, or electrical smell from a heat pump or "
            "any electrical appliance is a SAFETY hazard - not a cosmetic issue.\n"
            "- A fishy smell from a heat pump typically indicates overheating electrical "
            "components, mould/bacterial growth inside the unit, or motor failure. "
            "Spraying deodorizer or disinfectant is NOT an adequate repair.\n"
            "- Advise the tenant NOT to continue using the appliance until a qualified "
            "HVAC technician or electrician has inspected it.\n"
            "- The tenant should follow up in WRITING: 'The smell persists. Spraying "
            "did not resolve it. Please arrange a qualified technician inspection within "
            "14 days as required under s45.'\n"
            "- If the landlord does not arrange a qualified technician after written "
            "notice: Tribunal claim for s45 breach (failure to maintain safe premises) + "
            "work order requiring a qualified inspection (s78).\n"
            "- Cite s45 when drafting any notice or Tribunal application.\n"
            "GENERAL REPAIRS:\n"
            "- Landlord must maintain premises in reasonable repair under s45.\n"
            "- STEP 1: Serve a formal written 14-day notice to remedy under s56, citing "
            "the specific repairs and the landlord's obligations under s45(1)(b). "
            "Keep proof of delivery (sent via email, text, or hand-delivered).\n"
            "- STEP 2: If repairs are not completed within 14 days, apply to the Tenancy "
            "Tribunal for work orders, compensation for breach, and rent reduction if applicable.\n"
            "- Do NOT advise the tenant to withhold rent unilaterally - withholding rent "
            "without Tribunal approval exposes the tenant to a 14-day arrears notice and "
            "termination proceedings.\n"
            "What NOT to say:\n"
            "- Do NOT tell the tenant spraying disinfectant is an acceptable repair for "
            "an electrical or mechanical fault.\n"
            "- Do NOT say continued use of a suspect appliance is safe without inspection.\n"
            "- Do NOT say the tenant can simply withhold rent to force repairs - this is "
            "NOT a lawful self-help remedy without Tribunal approval.\n"
            "PREMISES UNCLEAN OR DAMAGED AT START OF TENANCY (s45(1)(a)):\n"
            "- Under s45(1)(a), the landlord must provide the premises in a reasonably clean "
            "condition at the start of the tenancy. If the tenant received keys to a filthy, "
            "infested, or unsanitary property, the landlord is in BREACH from day one.\n"
            "- A tenant who had no reasonable choice but to clean immediately (e.g., dead "
            "cockroaches, filth, unsafe conditions) is entitled to REIMBURSEMENT of reasonable "
            "cleaning and remediation costs.\n"
            "- Do NOT suggest the tenant is responsible for paying to make the premises "
            "liveable - this is the landlord's statutory obligation under s45(1).\n"
            "- The tenant should: (a) document the condition on entry (photos, written record), "
            "(b) send a formal written demand for reimbursement of cleaning and repair costs "
            "with receipts/invoices attached, and (c) apply to the Tribunal if the landlord "
            "refuses, claiming s45(1)(a) breach, cleaning costs, and any other remedial costs.",
    },

    // ── TENANCY AGREEMENT & PARTIES ───────────────────────────────────────────

    {
        .intent = "agreement_form",
        .include_any = {
            "tenancy agreement", "written agreement", "copy of agreement",
            "sign agreement", "signing agreement", "before signing",
            "provide agreement", "give the agreement", "before getting the agreement",
            "form of agreement", "written tenancy", "contents of agreement",
            "pet clause", "pet bond", "fish tank", "aquarium", "fish tank permission",
            "change payment date", "change my payment date", "payment date",
            "rent payment date",
            "pet regulations", "pet regulation", "report the landlord", "report landlord",
            "renew our contract", "renew our lease", "renew rental contract",
            "shorter term", "12 month fixed term", "minimum fixed term",
            "new agency", "agency changed", "new property manager", "pm changed",
            "changed property manager", "change of agency", "new pm",
            "discriminate on age",
            "landlord backed out", "landlord pulled out", "changed their minds",
            "changed our minds", "changed my mind", "we've changed our mind",
            "cancelled the tenancy", "pulled out of tenancy", "withdrew the offer",
            "going overseas", "overseas for", "travelling overseas",
            "away for weeks", "away for a month", "leaving the country",
            "add someone to the tenancy", "add someone onto the tenancy",
            "add a person to the tenancy", "add a flatmate to", "adding to the tenancy",
            "put someone on the tenancy", "put them on the tenancy",
            "add clauses", "adding clauses", "custom clause", "add to agreement",
            "adding to the agreement", "standard rta agreement",
            "payment summary", "rent payment summary", "rent history",
            "proof of rent payments", "proof of payment", "rent record",
            "listing photos", "listing pictures", "rental photos", "rental listing photos",
            "photos of the rental", "different from listing", "photos don't match",
            "old photos", "outdated photos",
            "search up tenant", "look up tenant", "check tenant record",
            "tenant record search", "find out about tenant", "tenant background check",
            "tenant checks", "tenant check form", "form for tenant checks",
            "check their record", "search their name", "tenant tribunal record",
            "search up someone", "put their name in", "entering their name",
            "search by first and last name", "first and last names",
            "credit report", "credit check", "credit history", "credit score",
            "tenancy applicant", "rental applicant", "application credit",
            "before moving in", "before handing over the keys", "money before moving in",
            "demand payment before", "fees before moving in", "costs before moving in",
            "letting fee", "parting with possession", "s44", "section 44",
            "assignment fee", "fee to assign",
            "put in a report about me", "report about me", "blacklisted", "blacklisted by landlord",
            "landlord reporting tenant", "rental blacklist", "bad rental reference",
            "previous landlord report", "blacklist me", "rental history report",
            "key money", "charging key money", "paid key money", "key fee",
            "accommodation supplement", "boarder payments", "board payments",
            "taking in boarders", "international students boarding",
            "2 weeks advance", "two weeks advance", "advance rent", "rent in advance",
            "weeks in advance", "pay on move in", "moving in costs", "weeks advance",
            "shared ground", "shared area", "communal area", "shared garden",
            "common area", "shared outdoor", "shared yard",
            "unregistered car", "unregistered vehicle", "car with no wof",
            "car wof", "no wof car", "car in the driveway", "car in driveway",
            "vehicle in driveway", "unreg car", "unreg wof",
            "finding fee", "tenant finding fee",
            "rental discrimination", "discriminating against tenants",
            "run a credit check", "conduct a credit check",
            "on a benefit", "declined because on a benefit", "benefit recipient",
            "discriminate against benefit", "beneficiary tenant", "winz benefit",
            "lettings fees", "lettings fee",
            "tenancy application form", "what is in a tenancy application",
            "tenancy application questions", "required on the application",
            "kainga ora", "from ko to", "ko to private", "state housing to private",
            "move from state", "moving from public housing", "housing new zealand",
            "from ko rental", "leaving ko", "hnz tenant", "ko house", "is it a ko",
            "is this a ko", "ko property",
        },
        .exclude_any = {
            "no agreement", "no written agreement", "no formal agreement",
            "without agreement", "there is no agreement", "no contract",
            "verbal agreement only", "nothing in writing",
            // Prevent "payment date agreed at Tribunal mediation" from landing here
            "tribunal order", "mediation order", "mediation agreement",
            "agreed at mediation", "order from tribunal",
            "mediation with tribunal", "response from bruce",
        },
        .forced_sections = {"NZLEG/RTA/s13A", "NZLEG/RTA/s13B"},
        .leg_allow_list = {
            "NZLEG/RTA/s13A",
            "NZLEG/RTA/s13B",
            "NZLEG/RTA/s13C",
        },
        .synthetic_query =
            "contents of tenancy agreement landlord obligations provide copy "
            "section 13A written tenancy agreement residential tenancies act",
        .notes = "Contents and copy obligations for tenancy agreements (s13A, s13B).",
    },

    {
        .intent = "bond",
        .include_any = {
            "bond lodgement", "bond lodged", "lodge the bond", "lodge bond",
            "bond receipt", "proof of bond", "bond proof",
            "bond before", "bond form", "bond help",
            "work and income", "winz", "bond guarantee",
            "can pay the bond", "pay the bond",
            "lodged my bond", "lodged the bond",
            "not lodged", "hasn't lodged", "has not lodged",
            "didn't lodge", "did not lodge", "never lodged", "failed to lodge",
            "paid bond", "paid a bond", "paid the bond",
            "paid for a bond", "paid for the bond",
            "bond in installments", "bond in instalments", "bond installments",
            "bond instalments", "bond payment plan", "pay bond in installments",
            "pay bond in parts", "pay the bond in",
            "give a bond", "gave a bond", "give bond",
            "bond sorted", "get my bond sorted", "bond for my rental",
            "set up my bond", "set up the bond",
            "bond history", "bond record", "bond on record", "lost their bond",
            "paid 1 week bond", "paid 2 weeks bond", "paid 3 weeks bond",
            "paid 4 weeks bond", "took a bond", "bond was taken",
            "bond refund", "refund my bond", "get my bond back", "bond back",
            "bond return", "return my bond", "return the bond",
            "bond refund form", "how long does bond", "bond timeframe",
            "when will i get my bond", "when do i get my bond",
            "bond reduced", "reduce the bond", "bond difference", "difference in bond",
            "bond amount reduced", "rent reduced bond",
            "bond delayed", "bond processing", "how long are bonds",
            "bonds taking", "bond taking", "bond still delayed",
            "holding my bond", "holding the bond", "pm holding bond",
            "how long does a bond", "bond take to be refunded", "bond refund time",
            "bond rejected", "bond application rejected", "bond stuck",
            "how long waiting for bond", "how long is everyone waiting",
            "bond wait time", "5-8 working days", "bond received forms",
            "bond still not received", "waiting for my bond",
            "credit card bond", "bond via credit card", "surcharge bond",
            "bond payment method", "lodging bond via", "bond with credit card",
            "bond surcharge", "surcharge for bond", "surcharge when lodging",
            "credit card surcharge", "lodging bond", "lodge bond via",
            "no receipts", "without receipts", "didn't provide receipts", "no evidence of costs",
            "take out of the bond", "take it out of bond", "take from the bond",
            "sort out the cleaners and take", "take cleaning from bond",
            "send bond", "sent bond", "sent the bond", "submit bond",
            "bond to tenancy", "bond direct", "bond himself", "bond herself",
            "bond number", "bond reference", "bond reference number",
            "different bond number", "wrong bond number",
            "s18",
        },
        .forced_sections = {"NZLEG/RTA/s18", "NZLEG/RTA/s19"},
        .leg_allow_list = {
            "NZLEG/RTA/s18",
            "NZLEG/RTA/s19",
            "NZLEG/RTA/s22",
            "NZLEG/RTA/s23",
        },
        .guidance_sources = {
            "MANUAL/how-to-apply-for-a-bond-refund",
            "MANUAL/bonds",
        },
        .synthetic_query =
            "general bond landlord maximum bond amount four weeks rent section 18 19 "
            "residential tenancies act bond lodgment duties receipt chief executive",
        .priority = 5,
        .notes = "General bond requirements - amount limits, receipt, lodgment (s18, s19).",
    },

    {
        .intent = "pet_bond",
        .include_any = {
            "pet bond", "pet bonds", "pet bond required", "pet bond amount",
            "pay a pet bond", "paying a pet bond", "charged a pet bond",
            "charging a pet bond", "asking for a pet bond", "want a pet bond",
            "bond for the cat", "bond for the dog", "bond for my pet",
            "bond for pet", "additional bond for pet", "extra bond for pet",
            "extra bond due to pet", "extra bond due to having pet",
            "additional bond due to pet", "bond due to having pet",
            "bond due to having pets", "bond due to having a pet",
            "bond because of pets", "bond because of having pets",
            "bond due to pets", "bond for my animal", "bond for the animal",
            "s18aa", "s18AA",
        },
        .forced_sections = {"NZLEG/RTA/s18", "NZLEG/RTA/s18A", "NZLEG/RTA/s18AA", "NZLEG/RTA/s19", "NZLEG/RTA/s22"},
        .leg_allow_list = {
            "NZLEG/RTA/s18",
            "NZLEG/RTA/s18A",
            "NZLEG/RTA/s18AA",
            "NZLEG/RTA/s19",
            "NZLEG/RTA/s22",
        },
        .synthetic_query =
            "pet bond cat dog animal s18AA 1 December 2025 retroactive "
            "landlord charge pet bond section 18AA residential tenancies act",
        .notes = "Pet bond rules (s18AA, 1 Dec 2025 regime) - separate from general bond.",
        .rule_card =
            "Pet bond rules (RTA s18AA, s18A, s22):\n"
            "PET BOND vs GENERAL BOND - THEY CAN COEXIST (CRITICAL LEGAL ACCURACY):\n"
            "- Under s18AA, pet bonds are a SEPARATE category from general bonds (s18).\n"
            "- A landlord CAN charge BOTH a general bond (up to 4 weeks rent, s18) AND a pet "
            "bond (up to 2 weeks rent, s18AA) for an approved pet. They are separate instruments.\n"
            "- The s18A restriction prohibits OTHER unlawful security deposits - it does NOT "
            "prevent pet bonds and general bonds from coexisting.\n"
            "- Do NOT say 'if a general bond has already been charged, a pet bond cannot also "
            "be charged' - this is WRONG. Both can coexist under s18 and s18AA respectively.\n"
            "LANDLORD vs PROPERTY MANAGER - WHO DECIDES:\n"
            "- The LANDLORD (not the PM/agency) makes the final decision on whether to charge "
            "a pet bond and how much (from $0 up to the 2-week maximum).\n"
            "- The PM administers communications and may relay the landlord's decision, but the "
            "decision to charge (or not) rests with the landlord.\n"
            "- If a tenant asks 'is it up to the landlord or the PM?', answer: the landlord "
            "decides, the PM implements.\n"
            "Pet bond retroactivity rule:\n"
            "- The pet bond regime (s18AA) took effect 1 December 2025. A landlord may only charge "
            "a pet bond for a pet that was agreed to IN WRITING on or after 1 December 2025.\n"
            "- EXISTING APPROVED PETS: If the tenant's pet was approved BEFORE 1 December 2025 "
            "(whether written or verbal), the landlord CANNOT charge a pet bond retroactively. "
            "Demanding one violates s18A.\n"
            "- NEW PETS added after 1 December 2025 can trigger a pet bond request - but it is "
            "OPTIONAL (the landlord's discretion); the landlord is NOT obliged to charge one.\n"
            "- If a tenant has an existing approved pet AND wants to add a NEW pet, the existing "
            "pet remains exempt; only the new pet can attract a pet bond.\n"
            "DOCUMENTING PRE-DEC 2025 APPROVAL:\n"
            "- If the pet was approved verbally or informally before Dec 2025, the tenant should "
            "seek WRITTEN CONFIRMATION from the landlord now (while the relationship is intact). "
            "A simple written statement (email or text) from the landlord confirming they approved "
            "the pet before December 2025 protects the tenant against future retroactive demands.\n"
            "Mid-tenancy bond deduction rule:\n"
            "- A landlord CANNOT deduct from the bond or claim insurance excess during a tenancy. "
            "Bond claims and insurance excess recovery can ONLY happen at tenancy end via the "
            "s22 process - the landlord cannot demand payment or deduct from the bond NOW "
            "while the tenancy is ongoing, regardless of what the insurance claim is for.\n"
            "- Never say a landlord can recover costs from the bond while the tenancy is ongoing.",
    },

    {
        .intent = "landlord_entry",
        .include_any = {
            "landlord entry", "landlord enter", "right of entry",
            "inspection notice", "24 hour notice", "24 hours notice",
            "routine inspection", "routine inspections",
            "landlord came in", "landlord access",
            "notice before entering", "notice to enter",
            "entered without notice",
            "s48",
            "home owner", "homeowner", "property owner", "owner of the house",
            "owner of the property", "owner came in", "owner entered",
            "entered my home", "came into my home", "already in my home",
            "was in my home", "was in my house", "was in my flat",
            "uninvited", "is she allowed to do this", "is he allowed to do this",
            "allowed to enter", "allowed to come in",
            "open home", "open homes", "viewings", "property viewing",
            "who is living", "who lives in", "who is residing", "occupants",
            "how many people", "who is staying",
            "prospective tenant", "showing the property", "showing a tenant",
            "showing my property", "showed my property",
            "48 hours notice", "48-hour notice", "48 hour notice",
            "reschedule inspection", "postpone inspection", "cancel inspection",
            "cancelled inspection", "inspection rescheduled", "inspection cancelled",
            "valuer", "valuation", "property valuer", "property valuation",
            "reinspection", "re-inspection", "second inspection", "follow-up inspection",
            "present at inspection", "be present", "attend inspection",
            "inspection time", "change inspection time",
            "inspection standard", "failed inspection", "not up to inspection",
            "insurance inspection", "mortgage valuation", "at owner's request",
            "send someone", "owner's request",
            "take photos", "photograph my", "photos of my",
            "unknown person in photo", "unknown male in photo", "stranger in photo",
            "privacy breach", "breach of my privacy", "breach of privacy",
            "final inspection", "exit inspection", "move out inspection",
            "outgoing inspection", "exist inspection",
            "keys handed in", "hand in keys", "handing in keys", "keys handed back",
            "landlord came over", "landlord come over", "landlord showed up",
            "landlord turned up", "turned up unannounced", "came over tonight",
            "came over without notice", "showed up without notice",
            "tested positive", "sick for inspection", "covid inspection",
            "ill for inspection", "unwell for inspection", "postpone due to",
            "delay inspection due",
            "can i decline", "can we decline", "can i refuse", "can we refuse",
            "refuse entry", "decline entry", "refuse the landlord", "decline the landlord",
            "landlord wants to come", "landlord wants to enter", "landlord wants access",
            "notified yesterday", "notice yesterday", "told yesterday",
            "less than 24 hours notice", "less than 24 hours",
            "only one day notice", "only 1 day notice",
            "hnz", "housing nz", "kainga ora entered", "come inside",
            "came inside", "come inside my house", "came inside my house",
            "first inspection", "what do PMs check", "what do they check",
            "what will they check", "inspection checklist",
            "door knock", "doorknock", "setting up an inspection", "schedule an inspection",
            "book an inspection", "arrange an inspection", "arrange inspection",
            "open cupboards", "look in cupboards", "look through cupboards",
            "open wardrobes", "look in wardrobes", "look through wardrobes",
            "go through wardrobes", "search through wardrobes", "go through drawers",
            "look through draws", "open drawers", "go through my belongings", "inspection scope",
            "what can they inspect", "what can landlord inspect",
            "open pantry", "pantry cupboard", "pantry door",
            "inspection level clean", "inspection-level clean",
            "how clean for inspection", "clean for inspection",
            "when is the next inspection", "when is my next inspection",
            "next inspection date", "ask when the inspection is",
            "when will the inspection be",
            "showing people", "showing potential tenants", "showing new tenants",
            "ask what time", "what time are they coming", "narrow down the time",
            "time window for inspection", "between 8am", "between 9am",
            "call out fee", "callout fee", "charging for access",
            "fee for not providing access", "access fee", "failed access charge",
            "inspection form", "form for inspection", "where to get inspection form",
            "inspection checklist form", "get a form for inspection",
            "specific time for inspection", "suggest a time for inspection",
            "propose a time", "request a time", "what time inspection",
            "inspection time preference", "choose inspection time",
            "insist on a reschedule", "right to reschedule", "force a reschedule",
            "reschedule the inspection", "demand a reschedule",
            "how often can a landlord", "inspection frequency", "frequency of inspections",
            "how many inspections", "inspections per year", "inspections per month",
            "3 months apart", "every 3 months", "every three months",
            "another inspection so soon", "inspection so soon", "inspection again so soon",
            "notice of inspection", "notice of the inspection",
            "cancelled the inspection", "inspection was cancelled",
            "pm cancelled", "agent cancelled", "inspection no show",
            "phone notice", "notice by phone", "notice over the phone",
            "notice via phone", "notice by text", "text notice", "texted notice",
            "appropriate notice for inspection", "valid notice for inspection",
            "property inspection", "charge for inspection", "inspection fee",
            "incoming inspection", "outgoing/incoming", "fee for inspection",
            "charging for inspection",
            "without you being there", "without the tenant being there",
            "entry without notice", "access without notice", "without proper notice",
            "inspection list", "end of tenancy inspection",
            "record me on my", "recording me on my", "record us on our",
            "neighbours recording", "neighbors recording", "neighbour recording",
            "neighbor recording", "record you on your property",
            "landlord's friend", "friend of the landlord", "friend of my landlord",
            "landlord's neighbour", "landlord's neighbor", "neighbour of the landlord",
            "neighbor of the landlord", "landlord sent someone", "someone the landlord sent",
            "gate remote", "spare key access", "someone with a key",
            "unauthorised person", "unauthorized person",
            "person who is not the landlord", "someone who is not the landlord",
            "person that is not the landlord", "someone that is not the landlord",
            "someone else entered", "someone else has been in", "someone else was in",
            "didn't let them in", "did not let them in", "let no one in",
            "person entered without", "entered my property without",
        },
        .exclude_any = {
            // Exit/final inspection for bond purposes is a different process (s22/s40),
            // not a landlord-entry question. Prevent landlord_entry from crowding out
            // exit_inspection_bond_process sections.
            "exit inspection", "final inspection", "move out inspection",
            "move-out inspection", "end of tenancy inspection",
            "bond refund process", "bond return process",
            "handed in my keys", "handed in our keys", "returned the keys",
            "after handing in the keys", "after moving out",
        },
        .forced_sections = {"NZLEG/RTA/s48", "NZLEG/RTA/s38"},
        .leg_allow_list = {
            "NZLEG/RTA/s48",
            "NZLEG/RTA/s38",
        },
        .synthetic_query =
            "landlord right of entry inspection notice 24 hours section 48 "
            "residential tenancies act access premises",
        .notes = "Landlord entry and inspection rules (s48).",
        .rule_card =
            "Landlord / agent entry rights (RTA s48):\n"
            "NOTICE REQUIREMENTS:\n"
            "- Landlord must give at least 24 HOURS written notice before entering "
            "for inspections, repairs, or showing to prospective tenants (s48(1)).\n"
            "- For BUYER VIEWINGS (prospective purchasers), the requirement is 48 HOURS "
            "written notice (s48(2)).\n"
            "- Notice must specify the date, time, and reason for entry.\n"
            "- Frequency cap: no more than once per 4-week period for routine inspections.\n"
            "AGENTS, CONTRACTORS AND THIRD PARTIES:\n"
            "- Agents, property managers, contractors, and ANY third party acting on "
            "behalf of the landlord must give the SAME notice as the landlord. There is "
            "NO exception for friends, neighbours, or the landlord's associates.\n"
            "- A neighbour or associate with a gate remote, spare key, or code does NOT "
            "have the right to enter the property without proper notice. The landlord "
            "cannot delegate entry rights that bypass the notice requirement.\n"
            "- Unauthorized entry by any third party (including the landlord's friend or "
            "neighbour) is a breach of the quiet enjoyment obligation (s38).\n"
            "TENANT RESPONSE TO UNAUTHORISED ENTRY:\n"
            "- Send a WRITTEN NOTICE to the property manager demanding that unauthorised "
            "access cease immediately. Retain copies of all communications.\n"
            "- If entry continues: the tenant can issue a TRESPASS NOTICE to the "
            "unauthorised person under the Trespass Act without needing the landlord's "
            "permission.\n"
            "- Apply to the Tribunal for: compensation for breach of quiet enjoyment (s38), "
            "and exemplary damages for serious or repeated unauthorized entry.\n"
            "What NOT to say:\n"
            "- Do NOT say the landlord or their associate can enter without notice because "
            "the landlord is overseas or because the person is a 'friend' or 'caretaker'.\n"
            "- Do NOT say the tenant must allow entry without proper written notice.",
    },

    {
        .intent = "sham_flatmate_agreement",
        .include_any = {
            "flatmate agreement", "flatmate arrangement",
            "boarder agreement", "boarder arrangement",
            "licence agreement", "licensee",
            "not a tenant", "not tenants", "are we tenants",
            "meant to be tenants", "should be tenants",
            "landlord not living", "landlord lives elsewhere",
            "landlord doesn't live", "landlord does not live",
            "landlord not resident", "landlord not there",
            "s5",
            "sublet", "sublets", "subletting", "sublease", "sub-letting", "sub-lease",
            "renting from a flatmate", "paying my flatmate", "flatmate charges",
            "flatmate is my landlord", "room from a flatmate",
            "he pays the landlord", "she pays the landlord",
            "paying through my flatmate", "pays the landlord for me",
        },
        .forced_sections = {"NZLEG/RTA/s5"},
        .leg_allow_list = {
            "NZLEG/RTA/s5",
        },
        .synthetic_query =
            "flatmate boarder licensee agreement landlord not resident sham tenancy "
            "RTA applies despite flatmate agreement section 5 definition residential "
            "tenancy landlord not living premises tenant rights wrongful agreement",
        .case_synthetic_query =
            "flatmate agreement landlord not living property sham tenancy RTA applies "
            "boarder licensee residential tenancy act tenant rights eviction notice "
            "invalid agreement landlord not resident",
        .notes = "Sham flatmate/boarder agreements where landlord is not resident (s5).",
    },

    // ── TENANCY LIFECYCLE ─────────────────────────────────────────────────────

    {
        .intent = "termination_notice",
        .include_any = {
            "evict", "evicted", "evicting", "eviction",
            "ask to leave", "asked to leave",
            "notice to leave", "notice to vacate",
            "end the tenancy", "end my tenancy", "terminate tenancy",
            "90 day notice", "90 days notice", "90-day notice",
            "42 day notice", "42 days notice", "42-day notice",
            "21 day notice", "21 days notice",
            "periodic tenancy end",
            "termination notice", "s51", "s56",
            "gave notice", "given notice", "i gave notice",
            "gave me notice", "given me notice", "received a notice", "received notice",
            "signed a variation", "tenancy variation", "variation agreement",
            "give notice", "giving notice", "handing in my notice", "hand in my notice",
            "give our notice", "sent our notice", "sent my notice", "give my notice",
            "minimum notice", "how much notice", "how many days notice",
            "moving out notice", "notice to move out", "notice period",
            "drop keys off", "drop the keys", "drop my keys", "drop keys to",
            "key return", "return the keys", "hand keys back",
            "extra days rent", "public holiday", "easter monday", "easter friday",
            "final rent payment", "last rent payment", "calculate rent",
            "work out rent", "work out my rent", "prorate rent", "pro rata rent",
            "calculate final", "final week rent",
            "resign lease", "re-sign lease", "resign the lease", "re-sign the lease",
            "renew the lease", "renew lease", "forced to renew",
            "tenancy ends", "tenancy is ending", "tenancy expires",
            "lease ends", "lease expires", "tenancy finishing",
            "go periodic", "go to periodic", "switch to periodic", "convert to periodic",
            "periodic instead", "periodic after fixed", "stay periodic",
            "end of fixed term", "fixed term ending", "fixed term expires",
            "change the locks", "changed the locks", "change locks", "lock change",
            "change my locks", "changed my locks", "new locks",
            "extend my stay", "extend the tenancy", "extend the lease",
            "extend my tenancy", "stay on after", "stay after the lease",
            "renew my tenancy", "notify about extending", "let pm know about extending",
            "not taking the extension", "not taking an extended", "won't be taking an extended",
            "not extending the fixed term", "won't be extending",
            "declining the extension", "not renewing the fixed term", "not renewing fixed term",
            "tenancy renews", "lease renews", "when my lease renews", "when my tenancy renews",
            "after the lease renews", "after the tenancy renews", "move after renewal",
            "move when lease renews", "just after renewal", "just after it renews",
            "final day of tenancy", "last day of tenancy", "move out day",
            "when does rent end", "rent until when", "rent paid until",
            "midnight of move out", "rent to wednesday", "rent to tuesday",
            "do i owe any rent", "do i owe rent", "owe any rent",
            "owe rent", "how much rent do i owe",
            "days rent", "last payment", "only owe", "owe for the last",
            "leave my rental", "leave the rental", "leaving my rental",
            "ending our tenancy", "ending tenancy", "how do we end",
            "how do i end my tenancy", "want to leave the rental",
            "last day of access", "how do i word the notice", "how do i word my notice",
            "wording for notice", "word my notice", "how to word notice",
            "do i still send", "do i still pay rent", "send the full amount",
            "still pay full rent", "pay full rent for last",
            "change to fixed term", "switch to fixed term", "convert to fixed term",
            "periodic to fixed term", "change from periodic to fixed",
            "force me to fixed term", "forced onto fixed term",
            "left without notice", "leave without notice", "abandoned the property",
            "tenant left without",
            "tenant won't vacate", "tenant wont vacate", "tenant refuses to vacate",
            "ensure tenant vacates", "tenant not vacating", "not left the property",
            "still in the property after", "still at the property after",
            "belongings left at the property", "left my belongings in",
            "belongings still in the property", "my stuff still at",
            "left belongings behind", "belongings left behind",
            "owner moving in", "owner wants to move in", "family moving in",
            "family member moving in", "90 day exception", "ninety day exception",
            "gone past the ninety", "no fault termination",
            "no-fault notice", "landlord moving in", "sold with vacant possession",
            "periodic vs fixed", "fixed vs periodic", "difference between periodic",
            "explain periodic", "what is a periodic", "what is periodic tenancy",
        },
        .forced_sections = {"NZLEG/RTA/s51", "NZLEG/RTA/s50", "NZLEG/RTA/s60A"},
        .leg_allow_list = {
            "NZLEG/RTA/s51",
            "NZLEG/RTA/s50",
            "NZLEG/RTA/s60A",
            "NZLEG/RTA/s52",
            "NZLEG/RTA/s53",
            "NZLEG/RTA/s13C",
            "NZLEG/RTA/s136",
        },
        .guidance_sources = {
            "MANUAL/giving-notice-to-end-a-tenancy",
            "MANUAL/ending-a-tenancy",
        },
        .synthetic_query =
            "landlord terminate periodic tenancy notice 90 days 42 days "
            "section 51 60A residential tenancies act tenant notice 21 days "
            "lawful grounds termination fixed term early termination section 50",
        .notes = "Termination notice - periodic (s51) and fixed-term protection (s50).",
        .rule_card =
            "Termination notice - fixed-term vs periodic (RTA s50, s51):\n"
            "CRITICAL: Check whether the tenancy is FIXED-TERM or PERIODIC before advising.\n"
            "FIXED-TERM TENANCY:\n"
            "- A landlord CANNOT end a fixed-term tenancy before the end date by giving "
            "a simple notice period. s51 (90-day/42-day/21-day notice) applies to PERIODIC "
            "tenancies only and does NOT apply to fixed-term tenancies.\n"
            "- On a fixed-term tenancy, early termination requires EITHER:\n"
            "  (a) Mutual written agreement between landlord and tenant under s50, OR\n"
            "  (b) A Tribunal order on statutory grounds (e.g., s55 for serious breach).\n"
            "- A unilateral '21-day notice to vacate' for rent arrears is NOT valid on a "
            "fixed-term tenancy. The correct process for rent arrears on fixed-term is:\n"
            "  1. s56 notice to remedy (14 days) for the rent arrears.\n"
            "  2. If not remedied, the landlord must apply to the Tribunal under s55 for "
            "  a termination order - they cannot simply issue a notice to vacate.\n"
            "- If the tenant receives what appears to be a termination notice on a "
            "fixed-term tenancy, they should NOT vacate unless the Tribunal has ordered it "
            "OR the fixed-term end date has actually passed.\n"
            "PERIODIC TENANCY:\n"
            "- Landlord can end by giving 90 days notice (s51(1)), or 42 days notice in "
            "specific circumstances (s51(2)), or 21 days notice on certain cause grounds.\n"
            "What NOT to say:\n"
            "- Do NOT say a 21-day notice is valid to terminate a fixed-term tenancy early.\n"
            "- Do NOT advise the tenant to vacate based on a landlord notice alone if the "
            "tenancy is still within its fixed term.",
    },

    {
        .intent = "fixed_term_sell",
        .include_any = {
            "fixed term tenancy sell", "fixed-term tenancy sell",
            "sell the house", "sell the property", "want to sell",
            "selling the house", "selling the property",
            "list the property", "list the house", "before listing",
            "vacant possession", "empty before", "vacant before",
            "fixed term end early", "break fixed term",
            "putting the property up for sale", "putting property up for sale",
            "putting it up for sale", "going up for sale", "going on the market",
        },
        // Exclude property-viewings-during-tenancy questions - those are about s47/s48
        // not vacant possession or fixed-term termination (s60A/s50).
        .exclude_any = {
            "open home", "open homes", "viewing", "viewings", "show the house",
            "showing the house", "show the property", "right of entry", "entry rights",
            "my rights in this", "rights in this situation", "rights in this case",
        },
        .forced_sections = {"NZLEG/RTA/s60A", "NZLEG/RTA/s50"},
        .leg_allow_list = {
            "NZLEG/RTA/s60A",
            "NZLEG/RTA/s50",
        },
        .synthetic_query =
            "landlord fixed term tenancy sell house vacant possession terminate early "
            "mutual agreement section 50 section 60A periodic tenancy notice "
            "residential tenancies act tenant rights fixed term expiry",
        .notes = "Landlord wants to sell with vacant possession during fixed-term (s60A, s50).",
    },

    {
        .intent = "healthy_homes",
        .include_any = {
            "healthy homes", "healthy home", "hhs",
            "heathly homes", "health homes", "health homes cert",
            "rainwater", "tank water", "rain water tank", "water quality",
            "potable water", "potable rainwater", "lead in water", "water contamination",
            "water standards", "safe to drink", "drinking water",
            "birds nesting", "bird nest", "nest in the", "nesting in", "swallows",
            "pest infestation", "vermin infestation", "infestation in the",
            "cockroach", "cockroaches", "rodent", "rodents", "rats in the", "mice in the",
            "mouse in the", "ants in the", "ants everywhere", "bugs in the",
            "bed bugs", "bed bug", "flea infestation", "fleas in the",
            "spider infestation", "wasps in the", "wasp nest",
            "HRV", "hrv system", "heat recovery ventilation", "hrv costs", "hrv unit",
            "heating standard", "heating requirement", "minimum heating",
            "insulation standard", "ceiling insulation", "underfloor insulation",
            "ventilation standard", "extractor fan", "extraction fan",
            "moisture barrier", "ground moisture", "draught stopping",
            "draught standard", "draughts", "draughty", "drafty", "drafts coming",
            "cold draft", "draft coming", "windows drafts", "draft from",
            "no insulation",
            "built before 1992", "built pre 1992", "pre-1992", "pre 1992",
            "built before 1 july 1992",
            "how does healthy homes", "how often checked", "healthy homes compliance",
            "healthy homes certificate", "compliance statement",
            "s138b", "s45b", "s66i",
            // Heating adequacy queries that belong in healthy_homes, not property_change
            "diesel heating", "alternative heating", "heating options",
            "alternative heat", "not enough heating", "inadequate heating",
            "no heating", "only has heating", "only heat source",
            "heat the house", "heat our home", "heat the home",
            "can't heat", "cannot heat", "too cold in winter",
            "hot water cylinder", "hot water system",
        },
        .forced_sections = {
            "NZLEG/RTA/s138B",
            "NZLEG/HHS/r8",
            "NZLEG/HHS/r14",
            "NZLEG/HHS/r21",
            "NZLEG/HHS/r23",
            "NZLEG/HHS/r26",
            "NZLEG/HHS/r28",
        },
        .leg_allow_list = {
            "NZLEG/RTA/s138B",
            "NZLEG/HHS/r6",
            "NZLEG/HHS/r8",
            "NZLEG/HHS/r14",
            "NZLEG/HHS/r21",
            "NZLEG/HHS/r23",
            "NZLEG/HHS/r26",
            "NZLEG/HHS/r28",
            "NZLEG/RTA/s78",
        },
        .synthetic_query =
            "healthy homes standards heating insulation ventilation moisture draught "
            "residential tenancies act section 138B landlord obligations "
            "extractor fan ceiling underfloor insulation draught stopping ground moisture barrier",
        .priority = 5,
        .notes = "Healthy Homes Standards - heating, insulation, ventilation, moisture, draught (HHS2019).",
        .rule_card =
            "Healthy Homes Standards guard (RTA s138B, HHS 2019):\n"
            "Private assessment NOT required for Tribunal:\n"
            "- A private Healthy Homes assessment is NOT legally required to make a Tribunal claim. "
            "The tenant's own evidence (photos, timeline, tenancy agreement, landlord correspondence, "
            "and any assessor's/insurer's reports already obtained) is sufficient.\n"
            "- Do NOT say the tenant must obtain or pay for a private HHA before applying to the Tribunal.\n"
            "If the tenant WANTS to get a Healthy Homes assessment:\n"
            "- Answer this question directly. A tenant CAN arrange an independent Healthy Homes "
            "assessment from any appropriately qualified building assessor or inspector.\n"
            "- Tenancy Services (tenancy.govt.nz) or MBIE can provide guidance on finding assessors.\n"
            "- An assessment can be USEFUL EVIDENCE in a Tribunal application but is not a legal "
            "precondition to filing. The tenant can file with or without one.\n"
            "- Do NOT over-apply the 'not required' point when the tenant is asking WHERE to get an "
            "assessment or how to build their evidence. Answer the practical question they asked.\n"
            "Heating obligation:\n"
            "- The LANDLORD must ensure the property meets the minimum heating standard (HHS r6). "
            "This cannot be shifted to the tenant.\n"
            "- Do NOT suggest the tenant should install their own heating system or pay to upgrade "
            "heating. The landlord bears the compliance cost.\n"
            "- If the existing heating is insufficient (e.g., old diesel, no fixed heater in living room), "
            "the landlord is in breach of the Healthy Homes heating standard.\n"
            "Heating device installed in the WRONG ROOM:\n"
            "- The HHS heating standard requires the heating device to be capable of heating the "
            "MAIN LIVING ROOM (the primary room used for everyday living), not a dining room, "
            "hallway, or secondary room.\n"
            "- If a landlord used the wrong room for the HHS heating calculation (e.g., a dining "
            "room instead of the actual lounge), they must: (a) confirm IN WRITING which room was "
            "used for the calculation, and (b) redo the assessment for the correct room, installing "
            "compliant heating in the correct main living room.\n"
            "- Tenant should REQUEST WRITTEN CONFIRMATION of which room was designated as the "
            "main living room for the HHS calculation. If the wrong room was used, the landlord "
            "is in breach and must remedy it.\n"
            "- Do NOT suggest compensation for increased power bills as a primary remedy - the "
            "correct remedy is a work order requiring the landlord to install compliant heating "
            "in the correct room.",
    },

    {
        .intent = "healthy_homes_facilities",
        .include_any = {
            "carport light", "laundry light", "no light", "lighting in",
            "lights in", "lights at", "adequate lighting", "working lights",
            "smoke alarm", "carbon monoxide",
        },
        .forced_sections = {
            "NZLEG/HHS/r21",
            "NZLEG/HHS/r23",
            "NZLEG/HHS/r24",
        },
        .leg_allow_list = {
            "NZLEG/HHS/r21",
            "NZLEG/HHS/r23",
            "NZLEG/HHS/r24",
        },
        .synthetic_query =
            "landlord obligations lighting smoke alarm carport laundry "
            "healthy homes standards ventilation extraction fan requirements "
            "habitable space facilities residential tenancy",
        .priority = 5,
        .notes = "HHS facilities: lighting, smoke alarms - forces ventilation sections as grounding context.",
    },

    // ── MONEY ────────────────────────────────────────────────────────────────

    {
        .intent = "rent_increase",
        .include_any = {
            "rent increase", "increase the rent", "raise the rent", "raised the rent",
            "increased the rent", "increased my rent",
            "increase my rent", "increase rent", "increasing rent", "increasing my rent",
            "raise the price", "raised the price", "increase the price", "raised price",
            "rent rise", "rent review", "maximum rent", "rent in advance",
            "weeks rent in advance", "how much rent", "notice to increase",
            "bidding war", "bidding wars", "rent bidding", "bid on rent",
            "rental auction", "rental bid", "asking for more rent",
            "short term lease higher", "higher rent for short", "charge more for short term",
            "short term premium", "shorter lease higher rent", "premium for short term",
            "short term lease", "short-term lease", "short term tenancy",
            "s28", "s28a",
        },
        .forced_sections = {"NZLEG/RTA/s24", "NZLEG/RTA/s28", "NZLEG/RTA/s28A"},
        .leg_allow_list = {
            "NZLEG/RTA/s24",
            "NZLEG/RTA/s28",
            "NZLEG/RTA/s28A",
            "NZLEG/RTA/s29",
        },
        .guidance_sources = {"MANUAL/rent-increases-and-reductions"},
        .synthetic_query =
            "rent increase 60 days written notice periodic tenancy section 24 12 months "
            "residential tenancies act lawful rent increase notice requirements",
        .notes = "Rent increases: s24 (standard notice), s28/s28A (order-based).",
    },

    {
        .intent = "fixed_term_rent_review",
        .include_any = {
            "rent review", "rent increase", "increase the rent", "raise the rent",
            "review clause", "rent review clause", "rent will increase",
            "rent going up", "review in", "increase at review",
        },
        .include_all = {"fixed term"},
        .forced_sections = {"NZLEG/RTA/s13A", "NZLEG/RTA/s50"},
        .leg_allow_list = {
            "NZLEG/RTA/s13A",
            "NZLEG/RTA/s50",
        },
        .synthetic_query =
            "fixed term tenancy rent review clause agreement contents section 13A "
            "landlord must specify review method limit mutual termination section 50 "
            "tenant options fixed term unable to pay increased rent",
        .notes =
            "Fixed-term tenancy with a rent review clause. "
            "s13A=agreement must clearly specify review terms (if silent, increase is invalid); "
            "s50=early exit options if tenant cannot afford the increase.",
    },

    {
        .intent = "tenant_early_exit",
        .include_any = {
            "leave early", "leave the tenancy early", "end the tenancy early",
            "move out before", "moving out before", "get out of the tenancy",
            "exit the lease", "exit the tenancy", "break the lease",
            "break lease", "breaking lease", "breaking the lease",
            "break fixed term lease", "breaking fixed term", "lease break",
            "lease break fee", "break fee", "break lease costs",
            "job offer", "new job", "job opportunity", "farm job",
            "relocating", "moving city", "moving town", "moving region",
            "partner got a job", "offered a job", "offered housing",
            "how do i get out", "how to get out", "how can i leave",
            "want to leave", "want to move out", "need to move out",
            "early termination tenant", "tenant terminate early",
            "unable to afford", "can't afford the rent", "cannot afford the rent",
            "cannot afford rent", "afford the rent anymore",
            "job loss", "lost my job", "lost our job", "income reduced", "reduced income",
            "end a fixed term", "end my fixed term", "end the fixed term",
            "end fixed term lease", "hardship",
            "medical reasons", "medical grounds", "health reasons", "due to medical",
            "kidney disease", "serious illness", "cancer", "locked in for",
            "locked into the tenancy", "locked in the fixed term",
            "doctor's letter", "doctor letter", "letter from my doctor",
            "take over the tenancy", "take over the lease", "take over my tenancy",
            "someone to take over", "find someone to take over", "replacement tenant",
            "get out of contract", "get out of the contract", "get out of my contract",
            "get out of the agreement", "rights to leave", "right to leave",
            "early termination", "fft early termination", "ftt early termination",
            "break our fft", "break our ftt", "break the fft", "break the ftt",
            "breaking my lease", "one tenant wants to leave", "one of us wants to leave",
            "co-tenant wants to leave", "joint tenancy one", "one flatmate leaving",
            "one of us is leaving", "one person leaving the tenancy",
        },
        .forced_sections = {"NZLEG/RTA/s50", "NZLEG/RTA/s66"},
        .leg_allow_list = {
            "NZLEG/RTA/s50",
            "NZLEG/RTA/s66",
        },
        .synthetic_query =
            "tenant fixed term tenancy leave early mutual agreement landlord consent "
            "section 50 termination agreement section 66 assignment subletting "
            "replacement tenant liability rent fixed term break lease early exit",
        .notes = "Tenant wants to leave a fixed-term early (job, relocation, hardship). s50=mutual termination, s66=assignment.",
    },

    {
        .intent = "carpark_dispute",
        .include_any = {
            "carpark", "car park", "car parks", "carparks",
            "parking space", "parking spaces", "parking bay",
            "parking included", "park my car", "use the garage",
            "garage included", "remove carpark", "lose carpark",
            "take away carpark", "vacate carpark", "vacate the carpark",
            "park on the driveway", "park in the driveway", "park on driveway",
            "driveway parking", "asked not to park", "told not to park",
            "cannot park on", "not allowed to park",
            "landlord keeping the garage", "keeping the garage",
        },
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s13A"},
        .leg_allow_list = {"NZLEG/RTA/s45", "NZLEG/RTA/s13A"},
        .synthetic_query =
            "landlord remove carpark parking space included tenancy agreement "
            "tenant facilities services agreed quiet enjoyment obligation "
            "section 45 landlord obligation section 13A tenancy agreement contents "
            "rent reduction loss of amenity agreed services",
        .priority = 8,
        .notes = "Carpark/parking dispute - landlord removing agreed facility. s45=landlord obligations, s13A=tenancy agreement contents.",
    },

    {
        .intent = "family_violence_exit",
        .include_any = {
            "protection order", "domestic violence", "family violence",
            "family harm", "violence order", "dv order",
            "women's refuge", "womens refuge", "refuge",
            "feeling unsafe at home", "unsafe in my home",
            "abusive partner", "abusive relationship",
            "s56b", "s56c", "s56d", "s56e",
        },
        // Family-violence withdrawal regime is s56B (right to withdraw),
        // s56C (notice/evidence service), s56D (remaining tenant's
        // termination right), s56E (disclosure restrictions). The earlier
        // route incorrectly referenced s55B (a LANDLORD termination ground
        // for unreasonable continuation — unrelated) and s55C (does not
        // exist in the RTA at all; phantom section caught by the PR #84
        // forced-section validator).
        .forced_sections = {"NZLEG/RTA/s56B", "NZLEG/RTA/s56C"},
        .leg_allow_list = {
            "NZLEG/RTA/s56B",
            "NZLEG/RTA/s56C",
            "NZLEG/RTA/s56D",
            "NZLEG/RTA/s56E",
        },
        .synthetic_query =
            "tenant family violence domestic violence protection order "
            "withdraw from tenancy s56B s56C s56D residential tenancies act "
            "victim safety notice evidence service remaining co-tenant",
        .notes = "Family violence withdrawal (s56B/C/D/E). NOT s55B (landlord "
                 "termination) and NOT s55C (does not exist).",
    },

    // ── TENANT RIGHTS & DISPUTES ──────────────────────────────────────────────

    {
        .intent = "quiet_enjoyment",
        .include_any = {
            "quiet enjoyment", "peaceful enjoyment", "peaceful possession",
            "s38",
            "harass", "harassed", "harassing", "harassment", "landlord harassing",
            "interfere with my belongings", "interfere with my possessions",
            "interfere with my stuff", "interfering with my",
            "get rid of my belongings", "get rid of my furniture", "remove my belongings",
            "forced to remove", "remove my stuff",
            "noisy neighbour", "noisy neighbor", "noisy neighbours", "noisy neighbors",
            "violent neighbour", "violent neighbor", "violent neighbours", "violent neighbors",
            "threatening neighbour", "threatening neighbor",
            "disruptive neighbour", "disruptive neighbor",
            "neighbour harassment", "neighbor harassment",
            "neighbour dispute", "neighbor dispute",
            "construction noise", "building works next door", "renovation next door",
            "noise complaint", "noise from neighbour", "noise from neighbor",
            "neighbour making noise", "neighbor making noise",
            "painters in my", "contractors in my", "workmen in my",
            "builders in our", "workmen in our", "tradespeople in our", "contractors in our",
            "builders in the rental", "builders doing repairs in",
            "unable to use bathroom", "unable to shower", "cannot use the bathroom",
            "rent reduction", "lower my rent", "lower the rent", "reduce my rent",
            "rent lowered", "rent should be reduced",
            "curtains missing", "no curtains", "lack of privacy", "privacy curtains",
            "sleep out", "sleep-out", "sleepout", "outbuilding",
            "landlord storing", "storing in my", "storing items in",
            "subdivided the property", "subdivision", "building work on the property",
            "construction on the property", "builders using", "lost use of driveway",
            "lost use of the driveway", "lost access to driveway", "lost access to the",
            "get my stuff from", "retrieve my belongings", "collect my belongings",
            "get my things from", "belongings in the garage", "can't get my stuff",
            "left belongings", "collect from the property", "pick up my stuff",
            "overnight guests", "guests spend the night", "spend the night",
            "stay the night", "family to stay", "friends to stay",
            "guests staying overnight", "visitors staying overnight",
            "not allowed to have guests", "guests not allowed to stay",
            "family and friends visit", "parents staying", "family visiting",
            "peace and enjoyment", "loss of peaceful enjoyment", "quiet enjoyment claim",
            "forbidden to", "not allowed to burn", "burn incense", "incense in my rental",
            "told i cannot", "told i can't", "pm told me i can't", "pm said i can't",
            "landlord rules about", "rules about what i can do",
            "come on to the section", "come onto the section", "builders on the section",
            "workers on the section", "access to the section", "onto my section",
        },
        .forced_sections = {"NZLEG/RTA/s38"},
        .leg_allow_list = {
            "NZLEG/RTA/s38",
            "NZLEG/RTA/s48",
        },
        .synthetic_query =
            "landlord obligation quiet enjoyment tenant peaceful possession "
            "section 38 residential tenancies act interference harassment "
            "noisy disruptive neighbours landlord must not interfere",
        .priority = 5,
        .notes = "Quiet enjoyment - landlord must not interfere with tenant's peaceful possession (s38).",
    },

    {
        .intent = "tribunal_process",
        .include_any = {
            "tenancy tribunal today", "applying to the tribunal", "apply to the tribunal",
            "apply to tribunal", "tribunal application", "tribunal process",
            "how does tribunal work", "how to apply to tribunal",
            "never done tribunal", "never been to tribunal", "never used tribunal",
            "file at tribunal", "file a claim", "lodge a claim",
            "evidence for tribunal", "provide evidence", "evidence at tribunal",
            "how do i apply", "what do i need for tribunal",
            "mediation", "hearing date", "tribunal hearing",
            "s85", "s86",
            "court order", "tribunal order", "order from tribunal",
            "going to court", "court tomorrow", "court today",
            "in court", "at court", "court hearing", "at the tribunal",
            "legal battle", "breach of tenancy", "breach of the tenancy",
            "repeat offender", "negligence",
            "applied to tribunal", "applied to the tribunal",
            "application to tenancy tribunal", "submitted an application",
            "tribunal address", "tribunal location", "where is the tribunal",
            "summoned to tribunal", "summoned to the tribunal", "tribunal summons",
            "going to tribunal", "going to the tribunal", "before the tribunal",
            "hard copies", "evidence submitted", "print out evidence",
            "timeframe to file", "time limit to apply", "deadline to apply",
            "how long to file", "how long to apply", "time to file",
            "appeal tribunal", "appeal the decision", "appeal to district court",
            "appealed tribunal", "appealed the decision",
            "hearing scheduled", "scheduled a hearing", "what to take to",
            "what to bring to", "advice for tribunal", "advice for hearing",
            "day of tribunal", "day of the hearing", "day of my hearing",
            "antisocial breach", "antisocial behaviour notice", "antisocial behavior notice",
            "disputing a breach", "dispute a breach", "dispute the breach",
            "false allegations", "false allegation", "false statements",
            "false evidence", "false rental reference", "false reference",
            "misleading tribunal", "lies at tribunal", "lied at tribunal",
            "spreading misinformation", "false information",
            "lawyer at tribunal", "legal representation", "need a lawyer",
            "do i need a lawyer", "lawyer at hearing", "solicitor at tribunal",
            "claiming more than", "claim more than $6000", "$6000",
            "how long for a decision", "waiting for decision", "tribunal decision time",
            "how long does tribunal take", "decision not made", "still waiting for decision",
            "haven't heard back", "still haven't heard", "how long did it take for a decision",
            "haven't heard from tribunal", "no decision yet",
            "won my tribunal case", "won the tribunal", "tribunal order paid",
            "how long to receive payment", "order says pay immediately",
            "tribunal says pay", "get paid from bond", "enforcement of order",
            "lost the hearing", "ordered to pay", "pay in instalments",
            "payment plan for", "weekly payments for", "instalment plan",
            "pay it weekly", "pay weekly", "pay in weekly",
            "letter of support", "anonymous witness", "keep me anonymous",
            "witness at tribunal", "support letter for tribunal",
            "should I lodge", "pay to lodge", "lodge with tribunal",
            "cost to lodge", "filing fee tribunal", "should I apply to tribunal",
            "how much does it cost to file", "cost to file an application", "how much to file",
            "put in an application", "advocate service", "tenant advocate", "advocacy service",
            "on appeals", "tribunal appeal", "experience on appeals",
            "is it a rehearing", "total rehearing", "rehearing at tribunal",
            "filed a lawsuit", "filed through tenancy services", "filed with tenancy services",
            "can i move before", "move before the process", "move during the claim",
            "move while claim", "still in the process", "process still ongoing",
            "claim for stress", "stress damages", "claimed for stress",
            "can they claim stress", "unnecessary stress", "distress damages",
            "claiming for stress", "claiming stress", "landlord claiming stress",
            "ordered to pay stress", "ordered to pay for stress",
            "name suppression", "name to be suppressed", "suppress my name",
            "anonymous at tribunal", "anonymise my name", "anonymize my name",
            "s109", "section 109", "12 month limitation", "limitation period",
            "exemplary damages time", "time limit for damages", "12 months to claim",
            "how long does it take to get a hearing", "how long for a hearing",
            "waiting for hearing date", "when will i get a hearing",
            "filed my application", "filed on the", "heard nothing from tribunal",
            "no response from tribunal", "tribunal not responding",
            "hold them accountable", "hold landlord accountable",
            "complaint to tenancy services", "complain to tenancy services",
            "done this to other tenants", "done this to previous tenants",
            "queue for hearing", "to be scheduled for hearing",
            "how long is the queue", "application pending hearing",
            "exemplary damages", "how many days to pay", "when must they pay",
            "how immediate", "pay us immediately", "must pay immediately",
            "enforcement of exemplary",
            "upload videos", "upload evidence", "video evidence",
            "upload to tribunal", "file type for tribunal", "upload files to case",
            "how to upload", "upload documents", "video to case",
            "worth going to tribunal", "worth attending the hearing",
            "worth going to hearing", "is it worth going to tribunal",
            "settled before hearing", "paid before tribunal", "paid me back before hearing",
            "already paid me back", "pm paid me back", "they paid me back",
            "submit to tenancy tribunal", "submit to the tribunal",
            "checklist for tribunal", "step by step for tribunal", "how to submit",
            "receive a verdict", "verdict after", "how long for a verdict",
            "decision after the hearing", "when will the decision come",
            "disputes tribunal",
            "tenancy compliance", "compliance and investigations", "compliance team",
            "tenancy compliance team", "compliance unit",
            "TT order", "TT orders", "dealing with tribunal", "phone mediation",
            "reasonable storage", "storage costs",
            "enforce a judgement", "enforce the judgement", "enforcing a judgement",
            "enforce a judgment", "enforce the judgment", "enforcement of judgement",
            "service of documents", "serve documents", "serving documents",
            "refusing to communicate", "refuse direct communication",
            "communicate through a third party", "3rd party communication",
            "all correspondence through", "correspondence through a third party",
        },
        .forced_sections = {"NZLEG/RTA/s77", "NZLEG/RTA/s85", "NZLEG/RTA/s86"},
        .leg_allow_list = {
            "NZLEG/RTA/s77",
            "NZLEG/RTA/s85",
            "NZLEG/RTA/s86",
            "NZLEG/RTA/s89",
            "NZLEG/RTA/s105",
            "NZLEG/RTA/s109",
        },
        .synthetic_query =
            "tenancy tribunal application process how to apply jurisdiction "
            "section 77 85 86 evidence mediation hearing residential tenancies act "
            "tenant landlord dispute claim procedure",
        .notes = "Tenancy Tribunal application process, evidence, hearings (s77, s85, s86).",
    },

    {
        .intent = "water_charges",
        .include_any = {
            "water bill", "water bills", "water charges", "water charge",
            "water usage", "water account", "water meter",
            "pay the water", "liable for water", "responsible for water",
            "water rates", "metered water", "water costs",
            "pay water", "water and waste", "wastewater", "water waste",
            "gas bottle", "gas bottle rental", "gas bottle fee", "gas bottle cost",
            "lpg bottle", "lpg gas", "lpg rental", "lpg fee", "lpg cost",
            "s36",
        },
        .forced_sections = {"NZLEG/RTA/s36"},
        .leg_allow_list = {
            "NZLEG/RTA/s36",
        },
        .synthetic_query =
            "landlord water charges tenant liable metered water supply "
            "section 36 residential tenancies act water bill payment "
            "water usage responsibility",
        .notes = "Water charge liability between landlord and tenant (s36).",
    },

    {
        .intent = "rent_arrears",
        .include_any = {
            "rent arrears", "arrears", "rent overdue", "overdue rent",
            "behind on rent", "behind in rent", "owe rent", "owes rent",
            "rent is behind", "rent behind", "behind with rent", "rent is overdue",
            "flatmate not paying", "flatmate misses paying", "flatmate owes",
            "flatmate missed rent", "flatmate behind on rent",
            "14 day notice", "14-day notice", "arrears notice",
            "notice for arrears", "unpaid rent", "missed rent",
            "rent not paid", "failed to pay rent",
            "rent payment day", "change rent payment day", "pay rent on",
            "when to pay rent", "rent due day", "rent payment schedule",
            "pay on a different day", "pay rent by", "rent is due on",
            "s55", "s56",
            "am i behind on rent", "am i behind with rent", "am i in credit",
            "in credit at end", "owing rent at the end", "owe rent at the end",
            "rent owing at end", "last week's rent owing", "last week rent owing",
            "rent calculation", "calculate rent owed", "work out my rent",
            "how much rent do i owe", "do i owe rent", "rent at the end of tenancy",
            "rent due today", "rent due tomorrow", "due today", "get paid tomorrow",
            "pay a day late", "rent due before payday", "pay rent the next day",
            "due on monday", "due on tuesday", "due on wednesday", "not paid until tomorrow",
        },
        .forced_sections = {"NZLEG/RTA/s55", "NZLEG/RTA/s27"},
        .leg_allow_list = {
            "NZLEG/RTA/s55",
            "NZLEG/RTA/s27",
            "NZLEG/RTA/s56",
        },
        .guidance_sources = {"MANUAL/rent-arrears-and-overdue-rent"},
        .synthetic_query =
            "tenant rent arrears unpaid rent landlord 14 day notice "
            "section 55 termination application tribunal section 27 "
            "rent payment obligation residential tenancies act",
        .notes = "Rent arrears, 14-day notice, termination for non-payment (s55, s27).",
    },

    // ── RULE CARDS ───────────────────────────────────────────────────────────

    {
        .intent = "electronic_notice_s13c",
        .include_any = {
            "text message", "sms", "email notice", "facebook message", "messenger",
            "whatsapp", "electronic notice", "served by email", "notice by text",
            "landlord texted", "tenant texted", "texted notice", "texted me notice",
            "emailed notice", "notice via email", "notice via text",
            "text message notice", "notice in a text", "notice sent by text",
            "sent notice by email", "receive notice by email", "notice by message",
            "is a text valid", "is an email valid", "can notice be by text",
            "can notice be by email", "valid notice by text", "valid notice by email",
            "sent a text", "sent by text", "via text", "via sms",
            "texted the landlord", "i texted", "we texted", "texted them notice",
            "texted notice to", "notice by texting",
        },
        .forced_sections = {"NZLEG/RTA/s136", "NZLEG/RTA/s13C"},
        .leg_allow_list = {"NZLEG/RTA/s136", "NZLEG/RTA/s13C", "NZLEG/RTA/s51"},
        .synthetic_query =
            "electronic communication notice text message email valid notice RTA "
            "section 136 service of documents electronic address written notice "
            "residential tenancies act s13C in writing tenant 21 days minimum notice "
            "periodic tenancy early notice valid no upper limit advance notice",
        .notes = "P0 guard: electronic/text/email notice validity and tenant-vs-landlord notice period rules.",
        .rule_card =
            "Electronic notice + tenant notice period rules (RTA s136, s51, s13C):\n"
            "Electronic messages and written notice:\n"
            "- Electronic messages (text, email, SMS) can satisfy 'in writing' requirements "
            "under s13C and s136. Validity depends on: whether the recipient gave that address "
            "for service, whether the notice was clear and unambiguous, and whether the required "
            "notice period was met.\n"
            "- Do NOT say a text or email is invalid merely because a section says 'written "
            "notice' - check s136 and s13C and the facts first.\n"
            "- Do NOT say a text or email is automatically valid in all cases either.\n"
            "TENANT vs LANDLORD notice period (CRITICAL - read before anything else):\n"
            "- Tenants ending a PERIODIC tenancy need a MINIMUM of 21 days notice (s51). "
            "Do NOT write s51(2A) - cite s51 only.\n"
            "- There is NO UPPER LIMIT on how far in advance a tenant can give notice. "
            "Giving notice 60, 90, or 105 days early is PERFECTLY VALID - more notice is better.\n"
            "- The 90-days rule in s51(1) and 42-days rule in s51(2) apply to LANDLORDS only.\n"
            "- s60A's 90-to-21 day window applies ONLY to FIXED-TERM tenancy non-renewal "
            "notices. It does NOT apply when a tenant terminates a periodic tenancy. Do NOT "
            "cite s60A or apply the 90-21 day window to a periodic tenancy situation.\n"
            "COMMON TENANT MISTAKE - CORRECT IT:\n"
            "- Tenants often WRONGLY believe their notice was invalid because it was 'too early' "
            "or 'outside the 90-21 day window'. This belief is INCORRECT for periodic tenancies. "
            "If a tenant says 'I gave notice 105 days ago, was that too early?' - the answer is "
            "NO. 105 days notice satisfies the 21-day minimum. The notice was VALID.\n"
            "- Do NOT echo back the tenant's mistaken belief. CORRECT IT directly.\n"
            "Retrospective landlord challenge - ANSWER TEMPLATE:\n"
            "- If the TENANCY HAS ALREADY ENDED and the landlord is now asking 'did you give "
            "21 days notice?', answer the question directly and POSITIVELY: confirm the original "
            "notice WAS valid, do not hedge.\n"
            "- If the facts show: (1) notice was sent electronically, (2) gave more than 21 days, "
            "(3) landlord received and acknowledged it - STATE CLEARLY that the notice WAS valid "
            "under s13C and s51. CONFIRM it, do not say 'appears' or 'may have been'.\n"
            "- MUST SAY: (a) text IS valid notice under s13C/s136, (b) 21 days minimum only - "
            "no upper limit for tenants, (c) landlord's actions (arranging inspection, "
            "acknowledging move-out) confirm they accepted the notice, (d) landlord CANNOT "
            "demand a 'final 21 day notice letter' - it has NO LEGAL BASIS.\n"
            "- Do NOT tell the tenant to give more notice now - the tenancy is over.\n"
            "- Do NOT say the tenant needs to do anything further regarding notice.\n"
            "- A tenant who gives MORE than 21 days notice has fully complied. If the notice "
            "gave well over 21 days (e.g., 60, 90, 105 days), say so clearly and confidently.",
    },

    {
        .intent = "fixed_term_mutual_agreement",
        .include_any = {
            "mutual agreement", "mutually agreed", "mutually agree",
            "both agreed to end", "both agreed to leave",
            "both parties agreed", "we both agreed",
            "agreed to leave", "agreed to move out", "agreed to finish",
            "agreed to terminate", "agreed to end", "we agreed to end",
            "agree to end", "agree to terminate", "agree to finish",
            "landlord agreed", "landlord and i agreed", "landlord said i can leave",
            "landlord let me leave", "landlord approved early",
            "agreed end date", "agreed to an end date", "agreed earlier end",
            "agreed on an earlier date", "landlord agreed to let me go",
            "landlord agreed we could leave",
            "confirmed end date", "confirmed the end date", "confirmed 13 march",
            "confirmed the move out date", "pm applying periodic",
            "property manager applying periodic", "pm applying notice rules",
            "give a date between", "give us a date to move", "give a move out date",
            "said yes on the phone", "new tenants want to move in",
            "landlord agreed on the phone", "agreed verbally", "verbal agreement",
        },
        .forced_sections = {"NZLEG/RTA/s50", "NZLEG/RTA/s60A", "NZLEG/RTA/s61"},
        .leg_allow_list = {
            "NZLEG/RTA/s50",
            "NZLEG/RTA/s60A",
            "NZLEG/RTA/s61",
        },
        .synthetic_query =
            "fixed term tenancy mutual agreement terminate early section 50 "
            "agreement to end tenancy s60A fixed term expiry liability "
            "section 61 abandonment mitigation residential tenancies act",
        .notes = "P1 guard: fixed-term mutual agreement (s50) displaces s60A default end-date liability.",
        .rule_card =
            "Fixed-term mutual agreement guard (RTA s50 vs s60A vs s61):\n"
            "CRITICAL - HOW TO OPEN YOUR ANSWER:\n"
            "If the facts show both parties already CONFIRMED an agreed end date in writing, "
            "DO NOT open with 'you may be liable until the end of the fixed term' or 'unless "
            "there was a mutual agreement'. That framing is factually wrong here. Instead, "
            "IMMEDIATELY state: 'Based on the written confirmation on [date], the agreed end "
            "date of [date] is the binding end date under s50.'  Then explain what follows.\n"
            "When the question shows BOTH PARTIES agreed on an earlier end date under s50:\n"
            "- That agreed date IS the binding end date. The tenancy ENDED on that date.\n"
            "- The tenant owes NO rent beyond the agreed end date.\n"
            "- The landlord CANNOT retroactively demand additional notice or extend the tenancy "
            "once both parties agreed on an end date and the tenant vacated on that date.\n"
            "- Do NOT apply s61 liability 'until original fixed-term end date' when s50 mutual "
            "agreement established an earlier binding end date.\n"
            "- Do NOT say the tenant is still liable for rent after the agreed-upon end date.\n"
            "- Do NOT say the PM can change the agreed end date after the tenant has already "
            "confirmed it in writing and relied on it.\n"
            "Priority rule: s50 mutual agreement OVERRIDES s60A fixed-term expiry default. "
            "Analyse s50 FIRST. Only apply s60A/s61 if there was NO mutual agreement to end early.\n"
            "Fixed-term vs periodic distinction: A fixed-term tenancy ends on its agreed date "
            "with NO notice required - a landlord who applies periodic-tenancy notice rules to "
            "a fixed-term that was mutually ended early is misapplying the law.\n"
            "IMPLICIT/BEHAVIORAL AGREEMENT: If the landlord's words or behavior showed they "
            "accepted an earlier end date - for example, (a) saying 'yes' on a phone call, "
            "(b) sending a text asking for a date range to move out, (c) conducting viewings "
            "with new tenants, or (d) any combination of these - this constitutes an agreement "
            "under s50 even if not formally confirmed in a single written document. The tenant "
            "confirming a specific date in writing after such exchanges is the binding end date.\n"
            "WHAT NOT TO SAY: Do NOT say the tenant needs to pay rent until the original "
            "fixed-term end date if s50 mutual agreement established an earlier date. Do NOT "
            "apply periodic tenancy 21-day notice requirements to a fixed-term that was "
            "mutually agreed to end early.",
    },

    {
        .intent = "tribunal_appeal",
        .include_any = {
            "appeal the decision", "appeal tribunal decision", "appeal a tribunal",
            "appealing tribunal", "appealing the decision", "appeal to district court",
            "lodging an appeal", "lodge an appeal", "file an appeal",
            "appeal to the district court", "district court appeal",
            "grounds for appeal", "can i appeal", "right to appeal",
            "want to appeal", "thinking of appealing", "considering an appeal",
            "disagree with the tribunal", "tribunal got it wrong", "tribunal made an error",
            "wrong decision by the tribunal",
            "appeal process", "the appeal process", "experience with the appeal",
            "how appeals work", "how the appeal works", "chances of appeal",
            "likely to change the decision", "likely to change the tribunal",
            "overturn the decision", "overturn tribunal", "reverse the decision",
            "rehearing", "re-hearing", "s117",
            "tribunal results", "got the tribunal results", "tribunal result",
            "tribunal gave me", "tribunal decided", "tribunal decision came",
            "unhappy with the tribunal", "upset with the tribunal", "upset about the tribunal",
            "not happy with the tribunal", "disagree with the result",
            "can i fight this", "can i challenge this decision", "challenge the outcome",
            "apeal", "appealing the tribunal",
            "likely this will change", "will this change the decision",
        },
        .forced_sections = {"NZLEG/RTA/s117"},
        .leg_allow_list = {
            "NZLEG/RTA/s117",
        },
        .synthetic_query =
            "appeal tenancy tribunal decision district court section 117 "
            "question of law grounds for appeal error jurisdiction rehearing "
            "upset tribunal result challenge decision",
        .notes = "P1 guard: tribunal appeal under s117 is on a question of law, not a fact rehearing.",
        .rule_card =
            "Tribunal appeal procedural facts (RTA s117):\n"
            "Always state ALL THREE procedural requirements when an appeal question is asked:\n"
            "1. DEADLINE: The notice of appeal must be filed within 10 WORKING DAYS of the "
            "Tribunal's decision (s117(6)).\n"
            "2. VENUE: Filed at the District Court NEAREST to where the Tribunal hearing was held "
            "(s117(5)). The other party will be served by the Registrar.\n"
            "3. GROUNDS: An appeal is on a question of LAW or a procedural/process error - NOT "
            "because you disagree with the outcome or want facts re-weighed. Mere dissatisfaction "
            "is NOT grounds. The District Court reviews legal correctness, not factual disputes.\n"
            "- Do NOT say disagreement with the outcome is sufficient grounds.\n"
            "- Do NOT say new evidence alone is enough.\n"
            "- Filing does not automatically stay the Tribunal's order (s117(10)).\n"
            "COMPENSATION FOR DISTRESS / FINANCIAL HARDSHIP:\n"
            "- The Tribunal can award compensation for stress, inconvenience, or financial "
            "hardship, but the EVIDENCE STANDARD IS HIGH.\n"
            "- Emotional distress, stress, needing to borrow money, or financial impact "
            "requires DOCUMENTED PROOF: bank records, loan agreements, medical evidence, "
            "or contemporaneous financial evidence. Speculative or general claims are "
            "unlikely to succeed without this documentation.\n"
            "- Do NOT say the claim was denied because 'you did not provide sufficient "
            "detail' - the bar requires DOCUMENTED EVIDENCE, not just more detail.\n"
            "- Do NOT tell a tenant they can simply claim for 'stress' or 'upset' without "
            "explaining the documentation requirement.\n"
            "PARTIAL SUCCESS IS STILL SUCCESS:\n"
            "- If the opposing party's main claims were denied and they only got a small "
            "amount (e.g., landlord got $50 but lost cleaning costs and garage door claims), "
            "this IS a significant outcome in the tenant's favour. Acknowledge this "
            "explicitly - it is NOT a loss for the tenant.\n"
            "- Do NOT frame a partial win for the tenant as a loss because some of the "
            "tenant's own claims also failed.\n"
            "- DO explicitly say: 'The landlord's cleaning and [other] claims being denied "
            "is a win for you. Only $50 was awarded to them.'\n"
            "DO NOT RECOMMEND 'ADDITIONAL EVIDENCE' FOR APPEAL:\n"
            "- Appeals are on LEGAL ERROR only. Do NOT include any step like 'prepare "
            "additional evidence or arguments' in your what-to-do list. That is NOT how "
            "appeals work. If there is no legal error in the Tribunal decision, there are "
            "no grounds to appeal, regardless of how much evidence the tenant has.\n"
            "- What you CAN say: 'Read the written reasons to identify if the adjudicator "
            "made a clear legal error. Without a legal error, an appeal is unlikely to succeed.'",
    },

    {
        .intent = "healthy_homes_room_count",
        .include_any = {
            "advertised as", "listed as", "marketed as", "described as bedroom",
            "described as a bedroom", "advertised as bedroom", "advertised bedroom",
            "how many bedrooms", "count the bedrooms", "number of bedrooms",
            "studio is a bedroom", "lounge as bedroom", "living room as bedroom",
            "room count", "bedroom count", "not a real bedroom", "tiny room",
            "room too small for bedroom", "sleeping area", "sleep in the lounge",
            "sleep in the living room", "living room converted to",
        },
        .forced_sections = {"NZLEG/HHS/r8", "NZLEG/HHS/r6"},
        .leg_allow_list = {
            "NZLEG/HHS/r8",
            "NZLEG/HHS/r6",
        },
        .synthetic_query =
            "healthy homes standards bedroom room count heating requirement "
            "qualifying heater main living room bedroom classification "
            "HHS regulations residential tenancy room",
        .priority = 5,
        .notes = "P1 guard: HHS bedroom/room classification - advertising alone is not conclusive.",
        .rule_card =
            "Healthy Homes room classification and heating compliance:\n"
            "MAIN LIVING ROOM / HEAT PUMP PLACEMENT:\n"
            "- Under HHS, the landlord must provide adequate heating for the ACTUAL main living "
            "room - the room used as the primary sitting/lounge area. Putting a heat pump in a "
            "different room (e.g. a dining room or converted room) and calling that the 'main "
            "living room' to satisfy HHS is non-compliant if the actual lounge is not adequately "
            "heated.\n"
            "- If the heat pump is in the wrong room: (1) REQUEST written confirmation from the "
            "landlord of which room they used as the 'main living room' for the HHS heating "
            "calculation; (2) DOCUMENT in writing that the actual lounge at the far end of the "
            "property is inadequately heated because the heat pump is not in or adjacent to it; "
            "(3) The landlord must REDO the HHS assessment for the correct main living room and "
            "provide compliant heating.\n"
            "BEDROOM CLASSIFICATION:\n"
            "- Do NOT rely solely on advertising wording to determine whether a room is a bedroom. "
            "Advertising is evidence but not conclusive.\n"
            "- Consider the actual room use, size, layout, and how the space was rented and used.\n"
            "PRACTICAL ADVICE:\n"
            "- Document all communications in writing. Give the landlord a reasonable opportunity "
            "to remedy the issue voluntarily before escalating to Tenancy Services.\n"
            "- The landlord must give proper notice and agree on a time for any inspection or "
            "remedial work under s48.",
    },

    {
        .intent = "repair_self_help_guard",
        .include_any = {
            "fix it myself", "fix myself", "arrange the repair myself",
            "arrange repairs myself", "organise the repair myself",
            "hire my own tradesperson", "hire a tradesperson myself",
            "get a tradesperson in myself", "sort it myself",
            "do the repair myself", "arrange it myself",
            "claim it back", "invoice the landlord", "bill the landlord",
            "claim reimbursement", "seek reimbursement", "get reimbursed",
            "take it off rent", "deduct from rent", "deduct cost from rent",
            "deduct repair cost", "deduct invoice", "withhold rent for repairs",
            "stop paying rent until", "withhold rent until repairs",
        },
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s46"},
        .leg_allow_list = {
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s46",
        },
        .synthetic_query =
            "tenant arrange repair reimbursement urgent repair landlord failed "
            "section 45 landlord obligations section 46 tenant remedy "
            "residential tenancies act repair cost claim",
        .notes = "P1/P2 guard: self-help repair reimbursement requires Tribunal order or landlord agreement.",
        .rule_card =
            "Repair self-help guard (RTA s45/s46):\n"
            "When a tenant is considering arranging repairs themselves and invoicing the landlord:\n"
            "- Do NOT simply advise 'arrange it yourself and invoice the landlord' without "
            "explaining the limits.\n"
            "- Reimbursement requires either the landlord's agreement or a Tribunal order.\n"
            "- Safe advice must include: (1) notify the landlord in writing first, (2) keep all "
            "invoices, (3) do not withhold rent unilaterally without proper legal steps.\n"
            "- Safe answer pattern: 'Notify the landlord first and document it. Only arrange "
            "urgent repairs yourself if the legal conditions are met, and keep all invoices - "
            "reimbursement is not guaranteed without a Tribunal order.'",
    },

    {
        .intent = "overclaim_guard",
        .include_any = {
            "old photos", "photos from years ago", "outdated photos",
            "photos not up to date", "misleading photos", "false photos",
            "photos don't match", "different to photos", "not what was advertised",
            "misrepresentation", "misled by", "misled me", "falsely advertised",
            "the listing said", "the ad said", "the advertisement said",
            "it was listed as", "it was described as", "property misdescribed",
            "wrong information in the listing", "inaccurate listing",
            "not as described", "listing was wrong", "ad was wrong",
        },
        .forced_sections = {"NZLEG/RTA/s13A"},
        .leg_allow_list = {
            "NZLEG/RTA/s13A",
        },
        .synthetic_query =
            "misleading advertising listing photos misrepresentation tenancy "
            "section 13A false inducement landlord tenant claim "
            "residential tenancies act advertising",
        .notes = "P2 guard: misleading listing/advertising - weak evidence is not automatic s13A liability.",
        .rule_card =
            "Advertising overclaim guard (RTA s13A):\n"
            "When a question involves old listing photos, misleading advertising, or a property "
            "not matching what was described:\n"
            "- Do NOT overstate the strength of the claim. Old photos or vague wording "
            "may support a complaint only if they materially misled the tenant or affected the "
            "tenancy agreement.\n"
            "- Weak evidence alone does not automatically create liability under s13A.\n"
            "- Safe answer pattern: 'This may help as evidence, but may not be enough on its own. "
            "The stronger argument is [X based on the specific facts in the retrieved context].'",
    },

    // ── SUBLETTING & ASSIGNMENT ───────────────────────────────────────────────

    {
        .intent = "subletting_without_consent",
        .include_any = {
            "sublet", "sublets", "subletting", "subletted", "subletter", "sub-let", "sub let",
            "can i sublet", "want to sublet", "allowed to sublet", "right to sublet",
            "subletting the property", "subletting the house", "subletting the room",
            "another person moving in", "someone else moving in", "extra person moving in",
            "tenant wants to sublet", "tenant is subletting",
            "flatmate without permission", "flatmate without consent",
            "adding a flatmate without", "new flatmate without",
            "consent to sublet", "permission to sublet",
            "landlord consent to sublet", "landlord permission to sublet",
            "landlord won't allow sublet", "landlord refused sublet",
            "refused subletting", "landlord refusing to allow",
            "parting with possession", "part with possession",
            "assignment of tenancy", "assign the tenancy", "assign my tenancy",
            "assigning the tenancy", "tenancy assignment",
            "transfer the tenancy", "tenancy transfer", "transfer my tenancy",
            "s44", "s43b", "s43B",
        },
        .exclude_any = {
            "family violence", "protection order",
            "healthy homes", "wear and tear",
        },
        .forced_sections = {"NZLEG/RTA/s44", "NZLEG/RTA/s43B"},
        .leg_allow_list = {
            "NZLEG/RTA/s44",
            "NZLEG/RTA/s43B",
            "NZLEG/RTA/s43",
        },
        .synthetic_query =
            "subletting without consent landlord permission assignment tenancy "
            "section 44 43B prior written consent reasonable grounds refusal "
            "residential tenancies act sublet flatmate parting with possession "
            "unlawful act unreasonable withholding",
        .notes = "Subletting/parting with possession and assignment of tenancy without landlord consent (s44, s43B).",
        .rule_card =
            "Subletting and assignment (RTA s44, s43B):\n"
            "- A tenant must have the landlord's prior written consent to sublet or part with possession (s44).\n"
            "- A tenant must also have prior written consent to assign the tenancy to someone else (s43B).\n"
            "- The landlord must not withhold consent unreasonably.\n"
            "- Subletting without consent is an unlawful act (s44(2)).\n"
            "- If the tenancy agreement expressly and unconditionally prohibits subletting, no subletting is permitted.\n"
            "- Safe answer pattern: Check whether the tenancy agreement has a no-subletting clause. "
            "If not, the tenant can sublet with prior written consent - the landlord cannot unreasonably refuse.",
    },

    // ── TRIBUNAL REPAIR ORDERS ────────────────────────────────────────────────

    {
        .intent = "tribunal_repair_order",
        .include_any = {
            "order landlord to fix", "order landlord to repair", "order them to fix",
            "order them to repair", "order the landlord to fix", "order the landlord to repair",
            "tribunal order repairs", "tribunal order repair", "tribunal orders repairs",
            "work order", "work orders", "repair order", "repair orders",
            "get an order for repairs", "order for maintenance", "order for work",
            "tribunal make landlord fix", "tribunal force landlord",
            "force landlord to repair", "force landlord to fix",
            "can tribunal order", "can the tribunal order", "tribunal order landlord",
            "get a work order", "apply for a work order", "apply for work order",
            "compensation for not repairing", "compensation for not fixing",
            "exemplary damages for not repairing", "damages for not fixing",
            "rent reduction order", "rent reduction while repairs",
            "withheld rent for repairs", "withhold rent for repairs",
            "s78", "s108",
        },
        .exclude_any = {
            "rent arrears", "non-payment", "14 day notice", "21 day notice",
        },
        .forced_sections = {"NZLEG/RTA/s78", "NZLEG/RTA/s108"},
        .leg_allow_list = {
            "NZLEG/RTA/s78",
            "NZLEG/RTA/s108",
            "NZLEG/RTA/s85",
            "NZLEG/RTA/s86",
        },
        .synthetic_query =
            "Tenancy Tribunal work order landlord repair maintenance "
            "section 78 108 orders remedies compensation exemplary damages "
            "failure to maintain residential tenancies act enforce order premises",
        .notes = "Tribunal ordering landlord to carry out repairs (s78 orders, s108 enforcement).",
        .rule_card =
            "Tribunal repair orders (RTA s78, s108):\n"
            "- The Tribunal can make a work order requiring a party to carry out specified repairs or maintenance (s78(1)(e)).\n"
            "- The Tribunal can also award exemplary damages or compensation where maintenance obligations have been breached.\n"
            "- An alternative money order may be substituted if a work order is not complied with (s78(2A)).\n"
            "- If a work order is not complied with, the tenant can apply for enforcement under s108, "
            "which may allow the work to be done at the non-complying party's expense.\n"
            "- Safe answer pattern: If the landlord refuses to repair despite written notice, "
            "apply to the Tribunal for a work order under s78. Keep written records of all repair requests.",
    },

    {
        .intent = "repair_notice_s56",
        .include_any = {
            // NOTE: bare "14 day notice" / "14-day notice" removed - too broad.
            // Matches "14 day notice for gardens" (property viewings context) causing contamination.
            "14 day notice to remedy", "14-day notice to remedy", "14 days notice to remedy",
            "notice to remedy",
            "formal notice to fix", "formal notice to repair", "notice to fix",
            "written notice to repair", "written notice to fix", "written notice for repairs",
            "notice to landlord to repair", "notice to landlord to fix",
            "landlord not fixing", "landlord not repairing", "landlord won't fix",
            "landlord won't repair", "landlord refuses to fix", "landlord refuses to repair",
            "landlord not acting", "landlord not actioning", "landlord ignoring repairs",
            "landlord ignoring my requests", "landlord still hasn't", "landlord still not fixed",
            "weeks and still not fixed", "months and still not fixed",
            "still waiting for repairs", "still waiting for the landlord",
            "pm still hasn't", "pm not following through", "pm not actioning",
            "next step for repairs", "next steps for repairs", "what do i do about repairs",
            "what can i do about repairs", "how do i get landlord to fix",
            "s56",
        },
        .exclude_any = {
            "bond", "bond refund", "tribunal results",
            // "appeal" (bare) replaced with legal phrases - bare "appeal" is polysemous
            // ("this option does not appeal to me") and is now boundary-safe but still
            // semantically wrong in the general case.
            "appeal the decision", "appeal the order", "appeal to the district court",
            "file an appeal", "filed an appeal", "filing an appeal",
            "notice of appeal", "appealing the decision", "appealed the decision",
        },
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s56"},
        .leg_allow_list = {
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s56",
        },
        .synthetic_query =
            "14 day notice to remedy breach landlord repair obligation "
            "section 56 notice landlord failure maintain premises residential tenancies act",
        .notes = "14-day notice to remedy for landlord repair failures (s56). Distinct from Tribunal work order.",
        .rule_card =
            "Repair process - notice before Tribunal (RTA s56, s45):\n"
            "When a landlord is not fixing repairs, the FIRST step is a FORMAL WRITTEN notice:\n"
            "- Serve a written 14-day notice to remedy under s56 identifying each repair and "
            "citing s45(1)(b) (landlord's duty to maintain premises in reasonable repair).\n"
            "- The notice must: identify the property, state the breach (each specific repair), "
            "give the landlord 14 days to fix it, and be signed and dated.\n"
            "- Only AFTER the 14 days expire without action should the tenant apply to the "
            "Tribunal for a work order (s78) or compensation.\n"
            "- Do NOT say the tenant should go directly to the Tribunal without first serving "
            "the s56 notice - the notice is a prerequisite and creates a paper trail.",
    },

    {
        .intent = "lease_break_fee",
        .include_any = {
            "lease break", "break lease", "breaking my lease", "break my lease",
            "lease break fee", "lease break fees", "break fee", "break fees",
            "breaking the lease", "break the lease", "break out of lease",
            "early termination fee", "early exit fee", "early end fee",
            "terminate fixed term early", "end fixed term early", "leaving fixed term early",
            "leave my fixed term early", "want to leave my fixed term",
            "break fixed term", "breaking fixed term", "exit fixed term",
            "reletting fee", "re-letting fee", "reletting costs", "re-letting costs",
            "costs for breaking", "penalty for breaking", "penalty for leaving early",
            "s44A", "s44a",
        },
        .forced_sections = {"NZLEG/RTA/s44A", "NZLEG/RTA/s38"},
        .leg_allow_list = {
            "NZLEG/RTA/s44A",
            "NZLEG/RTA/s38",
        },
        .synthetic_query =
            "lease break fee fixed term tenancy section 44A early termination "
            "reletting costs actual costs itemised invoice residential tenancies act",
        .priority = 5,
        .notes = "Fixed-term break fees (s44A) - actual costs only, no generic admin fees.",
        .rule_card =
            "Lease break fees (RTA s44A):\n"
            "When a tenant breaks a fixed-term tenancy early, the landlord may only claim:\n"
            "- ACTUAL AND REASONABLE costs of reletting (advertising, showing property, etc.)\n"
            "- Any difference in rent if the new tenant pays less (mitigated by s38 duty)\n"
            "UNLAWFUL GENERIC FEES (NAME THESE EXPLICITLY if present in the invoice):\n"
            "The following categories are business overheads and NOT lawful break-lease "
            "charges under s44A. If the invoice contains any of these, they must be disputed:\n"
            "- 'Admin fee', 'administration fee', 'administration costs'\n"
            "- 'Tenancy finalisation', 'finalisation fee', 'file closure'\n"
            "- 'Processing applications', 'application processing fee'\n"
            "- 'Preparing agreements', 'document preparation', 'paperwork fee'\n"
            "- 'Lease preparation fee', 'management fee', 'preset package'\n"
            "- Any bundled package like 'lease break package - $XXX' with no itemisation\n"
            "ADVERTISING COSTS:\n"
            "- Must be the ACTUAL COST of specific advertisements placed to find a new tenant.\n"
            "- Only the base Trade Me listing cost (or equivalent) is claimable.\n"
            "- Premium upgrades (featured listings, sponsored placements) cannot be charged "
            "unless the landlord can show these were genuinely necessary and justified.\n"
            "- Do NOT say advertising costs are always allowed at face value.\n"
            "INVOICE VALIDITY:\n"
            "- An invoice with no itemisation is INVALID. The tenant can refuse to pay and "
            "demand a full breakdown with receipts before paying anything.\n"
            "- The tenant should NOT pay before a replacement tenant is confirmed and actual "
            "costs are known.\n"
            "- The landlord has a DUTY TO MITIGATE (s38): they must actively seek a new tenant "
            "at the same or comparable rent. The tenant is NOT liable for costs beyond the "
            "date a replacement tenant starts paying rent.\n"
            "- Do NOT say the tenant must pay a preset 'lease break fee' or pay anything "
            "before a replacement tenant is found and actual costs are confirmed.",
    },

    {
        .intent = "tribunal_order_violation",
        .include_any = {
            // Existing order / sealed order references
            "tribunal order", "mediation order", "sealed order", "consent order",
            "the order says", "order requires", "bound by the order",
            "ignoring the order", "breached the order", "breaching the order",
            "violating the order", "not complying with the order",
            "enforce the order", "enforcement of the order", "compliance with the order",
            "comply with the order", "not following the order", "going against the order",
            "order from the tribunal", "tribunal made an order", "tribunal issued an order",
            "order has been issued", "order was issued",
            "pm ignoring order", "landlord ignoring order",
            "pm not following order", "landlord not following order",
            "pm overriding order", "landlord overriding order",
            // Mediation agreement phrasing (Q36-style: tenant had mediation, PM ignoring result)
            "mediation with tribunal", "had mediation with", "went to mediation",
            "we had mediation", "mediation agreement", "mediation result",
            "agreed at mediation", "agreed in mediation", "settled at mediation",
            "resolved at mediation", "mediation outcome", "result of mediation",
            "outcome of mediation", "at mediation we", "the mediation said",
            "email from tribunal", "email from the tribunal", "letter from tribunal",
            "letter from the tribunal", "tenancy tribunal email", "tribunal decided",
            "tribunal determined", "tribunal said to", "tribunal ruled",
            // PM/agency ignoring existing agreement
            "ignoring what was agreed", "ignoring the agreement",
            "ignoring the settlement", "ignoring the mediation",
            "went back on the agreement", "reverted back despite",
            "defaulted back despite", "gone back on what was agreed",
            "agency ignoring tribunal", "agency ignoring the order",
            "manager ignoring tribunal", "manager ignoring the order",
        },
        .exclude_any = {
            "apply for an order", "want to get an order", "can i get an order",
            "how do i get an order", "make an order", "apply to tribunal",
            "bond claim form along with the tribunal",
        },
        .forced_sections = {"NZLEG/RTA/s78", "NZLEG/RTA/s95A", "NZLEG/RTA/s38"},
        .leg_allow_list = {
            "NZLEG/RTA/s78",
            "NZLEG/RTA/s95A",
            "NZLEG/RTA/s38",
            "NZLEG/RTA/s86",
            "NZLEG/RTA/s109",
            "NZLEG/RTA/s56",
        },
        .synthetic_query =
            "Tenancy Tribunal order mediation settlement binding enforcement "
            "breach of order s78 orders exemplary damages s95A compliance "
            "residential tenancies act landlord non-compliance",
        .priority = 12,
        .notes = "Existing Tribunal/mediation order being ignored or breached - s78, s95A.",
        .rule_card =
            "Existing Tribunal order or mediation agreement (RTA s78, s95A, s38):\n"
            "When the user describes an existing Tribunal order, sealed order, consent order, "
            "or Tribunal-mediated settlement agreement being ignored:\n"
            "- Do NOT treat this as a new tenancy dispute starting from scratch. The order "
            "IS legally binding and cannot be overridden by the other party.\n"
            "- The lawful terms in the order (e.g., a payment date, bond split, repair "
            "obligation) take precedence over any subsequent informal demands.\n"
            "- RENT DUE DATE ORDERS: If a Tribunal mediation changed the rent due date, "
            "that new date is the LAWFUL due date. The tenant is NOT in arrears if they "
            "pay on the Tribunal-ordered date. A landlord issuing incorrect notices based "
            "on the old date is in breach of good faith under s38.\n"
            "- If a party is breaching a Tribunal order, the correct steps are:\n"
            "  1. Send a FORMAL WRITTEN reminder citing the order and demanding compliance.\n"
            "  2. Apply to the Tribunal to enforce the order (s78 gives the Tribunal power "
            "     to make compliance orders and award exemplary damages for breach).\n"
            "  3. Do NOT simply ignore further incorrect notices - document and respond to "
            "     each one in writing, referring back to the binding order.\n"
            "- The party bound by a Tribunal order who fails to comply may face exemplary "
            "damages under s95A for unlawful acts or further Tribunal orders.\n"
            "- Do NOT say the tenant must re-agree to terms already settled by Tribunal order.",
    },

    {
        .intent = "repairs_landlord_not_fixing",
        .include_any = {
            // Appliance-specific: require the appliance word + broken/not working
            "broken oven", "oven broken", "oven not working", "oven doesnt work",
            "oven was broken", "oven is broken", "oven has been broken",
            "oven still broken", "oven still not", "oven timing out",
            "broken stove", "stove broken", "stove not working",
            "stove was broken", "stove is broken",
            "broken dishwasher", "dishwasher broken", "dishwasher not working",
            "broken fridge", "fridge broken", "fridge not working",
            "broken heater", "heater broken", "heater not working",
            "broken hot water", "hot water not working", "no hot water",
            "no running water",
            "no heating", "heating not working", "heating broken",
            // Duration-of-neglect triggers (compound: broken + time)
            "broken for months", "broken for weeks", "not fixed for months",
            "not fixed for weeks", "not repaired for months", "not repaired for weeks",
            "still not fixed", "still broken", "still not repaired",
            "hasn't been fixed", "havent been fixed", "have not been fixed",
            "still not been fixed", "never been fixed",
            "waiting months for", "months waiting for repair",
            // Landlord-refusal compound triggers
            "landlord not fixing", "landlord not repairing",
            "landlord wont fix", "landlord won t fix", "landlord refuses to fix",
            "landlord refuses to repair", "landlord ignoring repairs",
            "pm not fixing", "pm wont fix", "pm refusing to fix",
            "pm ignoring the repair", "pm ignoring repairs",
            "property manager not fixing", "property manager wont fix",
            // General appliance failure
            "appliance broken", "appliance not working", "broken appliance",
        },
        .exclude_any = {
            "rent arrears", "notice to remedy", "14 day notice to remedy",
            "tribunal results",
            // "appeal" (bare) replaced - polysemous; use specific legal phrases only
            "appeal the decision", "appeal the order", "appeal to the district court",
            "file an appeal", "filed an appeal", "filing an appeal",
            "notice of appeal", "appealing the decision", "appealed the decision",
            // Exclude tenant-caused damage (tenant broke it, not landlord failing to fix)
            "i broke", "i accidentally", "i cracked", "my fault", "i damaged",
            "accident where i", "fell off and cracked", "i spilled", "i knocked",
        },
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s56", "NZLEG/RTA/s77", "NZLEG/RTA/s78"},
        .leg_allow_list = {
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s56",
            "NZLEG/RTA/s77",
            "NZLEG/RTA/s78",
        },
        .synthetic_query =
            "landlord not fixing broken appliance maintenance repair obligation "
            "section 45 landlord responsibilities notice to remedy s56 Tribunal "
            "work order s78 compensation residential tenancies act",
        .notes = "Broken appliance / landlord not fixing - practical repair path s45/s56/s77/s78.",
        .rule_card =
            "Broken appliance / landlord not fixing (RTA s45, s56, s77, s78):\n"
            "When a landlord is failing to fix a broken appliance or maintenance issue:\n"
            "Step 1 - formal written notice:\n"
            "- Serve a written 14-day notice to remedy under s56, identifying each defect, "
            "citing s45(1)(b) (landlord duty to maintain premises in reasonable repair), "
            "and warning that Tribunal application will follow if not remedied.\n"
            "Step 2 - gather evidence:\n"
            "- Document all communications (texts, emails, inspection dates) and obtain a "
            "written assessment from a tradesperson confirming the appliance is defective. "
            "This is key evidence for any Tribunal claim.\n"
            "Step 3 - if landlord still does not fix:\n"
            "- Apply to the Tenancy Tribunal for: a work order requiring the repair (s78), "
            "compensation for demonstrable losses (increased costs, loss of use), and "
            "exemplary damages if the breach has been persistent and wilful.\n"
            "- For SAFETY defects (e.g., gas appliance, unsafe oven): emphasise urgency and "
            "the landlord's immediate obligation - the tenant should not use an unsafe appliance.\n"
            "What NOT to say:\n"
            "- Do NOT advise the tenant to break the lease or abandon the property as a first "
            "response to an unrepaired appliance. The correct remedy is formal notice then Tribunal.\n"
            "- Do NOT say the tenant has no recourse until the landlord acts - the notice and "
            "Tribunal path gives the tenant direct enforcement tools.",
    },

    {
        .intent = "exit_inspection_bond_process",
        .include_any = {
            "exit inspection", "final inspection", "move out inspection", "move-out inspection",
            "inspection when moving out", "inspection after moving out",
            "bond inspection", "pre inspection", "property inspection before bond",
            "final walkthrough", "end of tenancy inspection",
            "landlord inspection before bond", "inspection for the bond",
            "bond refund process", "bond refund how long", "how long for bond",
            "when will i get my bond", "when do i get my bond back",
            "bond return process", "bond claim process", "how to claim bond",
            "how do i claim my bond", "bond after moving out", "bond after i move",
            "reasonable time for inspection", "how long does inspection take",
            "pm taking long to inspect", "waiting for inspection",
            "waiting for exit inspection", "exit inspection delay",
            // Bond refund delays and form submission
            "signed the release bond form", "signed the bond form", "release bond form",
            "bond form signed", "signed bond refund form", "bond release form",
            "get my bond money back", "get bond money back", "bond money back",
            "get bond back", "haven't got bond back", "still haven't got bond",
            "haven't received the bond", "still waiting for the bond",
            "3 months and no bond", "months and no bond", "months without bond",
            "weeks without bond", "waiting months for bond",
        },
        .exclude_any = {
            "inspection report",
            "tribunal order bond", "bond after tribunal",
        },
        .forced_sections = {"NZLEG/RTA/s22", "NZLEG/RTA/s40"},
        .leg_allow_list = {
            "NZLEG/RTA/s22",
            "NZLEG/RTA/s40",
            "NZLEG/RTA/s49A",
            "NZLEG/RTA/s49B",
            "NZLEG/RTA/s23",
        },
        .synthetic_query =
            "exit inspection bond refund process move out final inspection "
            "section 22 bond claim evidence photos tenant obligations "
            "residential tenancies act bond return",
        .notes = "Exit inspection timing and bond refund process (s22, s40).",
        .rule_card =
            "Exit inspection and bond refund process (RTA s22, s40):\n"
            "IF BOND HASN'T BEEN REFUNDED AFTER MOVE-OUT:\n"
            "- The standard bond refund timeframe is 2-5 WORKING DAYS once a signed "
            "refund form is submitted to the Bond Centre. Three months is far beyond "
            "normal - the tenant should NOT wait passively.\n"
            "- STEP 1: Contact Tenancy Services (0800 836 262 or tenancy.govt.nz) to "
            "verify whether the signed bond form was ever lodged with the Bond Centre.\n"
            "- STEP 2: If the form was NOT lodged, the tenant can file a TENANT-ONLY "
            "refund application (Tenancy Services online portal) WITHOUT the landlord's "
            "permission or signature. The tenant does not need the landlord to do anything.\n"
            "- STEP 3: If the landlord disputes the refund, they must apply to the "
            "Tenancy Tribunal. They CANNOT simply hold the bond indefinitely - that is "
            "unlawful. A landlord who delays or fails to return the bond without a "
            "genuine dispute can be ordered to pay exemplary damages.\n"
            "The exit/final inspection is a PRACTICAL step, not a statutory holding point:\n"
            "- There is no fixed statutory deadline for a landlord to complete an exit "
            "inspection. A reasonable expectation is 1-3 working days after move-out, but "
            "the RTA does not specify an exact timeframe.\n"
            "- The BOND REFUND PROCESS (s22) is what matters legally. A landlord must "
            "apply for bond deductions within a reasonable time; delay does not indefinitely "
            "hold the tenant's bond.\n"
            "Tenant protection - evidence:\n"
            "- The tenant's OWN PHOTOS taken at handover are crucial evidence. These protect "
            "the tenant regardless of when or how the formal exit inspection is done.\n"
            "- Take timestamped photos/video of every room, appliance, and surface at "
            "move-out. Send a follow-up message to the PM confirming you have vacated and "
            "noting any pre-existing conditions documented.\n"
            "PRE-EXISTING DAMAGE:\n"
            "- If damage was visible at move-in (even if NOT formally documented in the "
            "property report), the landlord CANNOT charge the tenant for it. The landlord "
            "must prove the damage occurred DURING the tenancy - if the damage was pre-existing, "
            "there is no proof it was caused by the tenant.\n"
            "- Example: sheer curtains that were already beginning to tear at move-in are NOT "
            "chargeable at exit. The burden of proof is on the LANDLORD, not the tenant.\n"
            "- The tenant should state in writing: 'This damage was pre-existing and visible "
            "at move-in. You have no evidence it occurred during our tenancy.'\n"
            "CURTAIN AND SOFT FURNISHING CLEANING:\n"
            "- Large, fixed curtains (sheers, curtains attached to rails) are NOT the same "
            "as moveable soft furnishings. The tenant's obligation is to leave the premises "
            "REASONABLY CLEAN AND TIDY (s40), which for fixed curtains means tenant-level "
            "cleaning: vacuuming, wiping dust, spot-cleaning visible marks.\n"
            "- Professional dry-cleaning or laundering of large fixed curtains is NOT a "
            "standard tenant obligation unless actual damage (torn, stained beyond normal use) "
            "requires it. A landlord demand for professional curtain cleaning where curtains are "
            "simply dusty or lightly soiled is excessive and should be declined.\n"
            "SAMPLE RESPONSE TO LANDLORD:\n"
            "- When the tenant needs to respond to an exit inspection charge list, provide a "
            "SAMPLE RESPONSE EMAIL that is factual, firm, and polite. Structure it as:\n"
            "  (1) Acknowledge items the tenant accepts responsibility for.\n"
            "  (2) Dispute pre-existing items, stating the landlord has no evidence they "
            "occurred during the tenancy.\n"
            "  (3) Decline unlawful charges (void pet clause, excessive cleaning demands).\n"
            "  (4) Request ITEMIZED PROOF for each claim before any payment is agreed.\n"
            "- The burden of proof for ALL claimed damage is on the LANDLORD. The tenant "
            "should not pay anything without seeing the landlord's evidence.\n"
            "PET CLAUSE PROFESSIONAL CLEANING REQUIREMENTS:\n"
            "- A tenancy agreement clause requiring PROFESSIONAL CARPET CLEANING at the "
            "end of the tenancy (regardless of actual damage or condition) is VOID and "
            "UNENFORCEABLE under s11(1)(b) RTA. It imposes an obligation beyond what "
            "the Act requires.\n"
            "- The RTA only requires the tenant to leave the property in a REASONABLY "
            "CLEAN AND TIDY CONDITION (s40). If professional cleaning is not needed to "
            "restore the property to that standard, the tenant is NOT liable for it.\n"
            "- Do NOT suggest the tenant should pay for professional cleaning 'as a "
            "compromise' or to avoid dispute. The legal position is clear: only actual "
            "damage and cleaning needed to restore reasonable condition is chargeable.\n"
            "- The tenant should respond in writing: 'The pet clause requiring professional "
            "cleaning regardless of actual condition is void under s11(1)(b) RTA. We are "
            "prepared to address any actual damage, but will not pay for professional "
            "cleaning where none was required to restore the premises.'\n"
            "VIEWINGS BY NEW TENANTS:\n"
            "- If the landlord is conducting viewings for new tenants while the outgoing "
            "tenant is waiting for the exit inspection, those viewings do NOT delay the "
            "inspection or affect the tenant's bond entitlement. The two processes are "
            "independent. Do NOT say the tenant must wait for viewings to finish.\n"
            "What NOT to say:\n"
            "- Do NOT confuse the exit inspection for bond purposes with routine landlord "
            "entry for viewings or routine inspections - these are different processes.\n"
            "- Do NOT say the PM is legally required to complete the exit inspection same "
            "day or within 24 hours - no such rule exists.\n"
            "- Do NOT say the tenant cannot receive their bond until the exit inspection "
            "is done - if the PM is unreasonably delaying, the tenant can apply to the "
            "Tribunal for bond release.\n"
            "- Do NOT suggest the tenant should pay for professional cleaning as a "
            "compromise when the legal position (s11 void clause) is in their favour.",
    },

    {
        .intent = "pet_permission",
        .include_any = {
            "get a dog", "keep a dog", "have a dog", "want a dog", "get a pet",
            "keep a pet", "have a pet", "want a pet", "get a cat", "keep a cat",
            "have a cat", "want a cat", "allow pets", "allow a pet", "allow a dog",
            "allow a cat", "can i have a pet", "can i get a pet", "can i keep a pet",
            "can we have a pet", "can we get a pet", "can we keep a pet",
            "can i have a dog", "can i get a dog", "can i keep a dog",
            "can we have a dog", "can we get a dog", "can we keep a dog",
            "landlord refuse pet", "landlord refuse a pet", "landlord refuse a dog",
            "landlord refuse my pet", "landlord can refuse",
            "landlord refuses pet", "landlord refuses dog",
            "refuse my request for a pet", "refuse my pet request",
            "new pet rules", "new rules for pets", "new rules about pets",
            "new rules coming into place", "new rules for dogs",
            "pet request", "pet application", "request to keep a pet",
            "permission for a pet", "permission to have a pet",
            "s42e", "s42E", "s42f", "s42F",
        },
        .exclude_any = {
            "pet bond", "pet bonds", "s18aa", "s18AA",
            "cockroach", "cockroaches", "rodents", "pest infestation",
            "bed bug", "bed bugs", "flea infestation", "rat infestation",
            "mouse infestation", "mice infestation", "wasp nest", "ant infestation",
        },
        .forced_sections = {"NZLEG/RTA/s42E"},
        .leg_allow_list = {"NZLEG/RTA/s42E"},
        .synthetic_query =
            "tenant written request keep pet dog breed size care plan 21 days respond "
            "automatic consent silence section 42E process new pet rules how to request "
            "pet permission residential tenancies act December 2025",
        .case_synthetic_query =
            "section 42E tenant pet request 21 days automatic consent December 2025 "
            "new pet rules written request process landlord response timeframe",
        .notes = "Pet permission request process (s42E/s42F) - 21-day rule, silence = consent.",
        .rule_card =
            "Pet permission process (RTA s42E - effective December 2025):\n"
            "NOTE ON CASE LAW: The new pet rules (s42E) took effect December 2025. Older "
            "Tribunal decisions about landlord pet refusal apply DIFFERENT rules that predated "
            "s42E. Do NOT rely on pre-December-2025 cases as the current legal position.\n"
            "HOW TO ANSWER 'Can my landlord refuse me a pet?':\n"
            "The correct answer is: YES, a landlord CAN refuse - BUT only if they respond in "
            "writing within 21 DAYS with a SPECIFIC REASONABLE GROUND. If they do NOT respond "
            "within 21 days, consent is AUTOMATICALLY GRANTED under s42E (silence = consent). "
            "Always explain BOTH the refusal possibility AND the 21-day auto-consent protection "
            "in the same breath. Do NOT fabricate refusal grounds the landlord has not stated.\n"
            "ANTI-HALLUCINATION (check this FIRST before writing anything):\n"
            "Step 1: Has the landlord ACTUALLY refused in this question?\n"
            "- If NO (tenant asks 'CAN my landlord refuse me?', not 'my landlord HAS refused'): "
            "Do NOT describe any refusal. Do NOT write 'In your case, the landlord has provided "
            "specific reasons...' - no refusal has occurred yet. Describe the PROCESS first.\n"
            "- If YES (landlord has explicitly refused): address the refusal, ask for reasons, "
            "assess whether grounds are reasonable under s42F.\n"
            "REQUIRED CONTENT - include ALL THREE in every answer:\n"
            "(1) WRITTEN REQUEST: The tenant must first submit a WRITTEN REQUEST specifying the "
            "pet's breed, age, size, and care plan. This starts the 21-day clock under s42E.\n"
            "(2) 21-DAY AUTO-CONSENT: The landlord has 21 DAYS to respond in writing. If the "
            "landlord does NOT respond within 21 days, consent is AUTOMATICALLY GRANTED - "
            "silence equals consent under s42E. State this explicitly every time.\n"
            "(3) IF REFUSED: The tenant should ask for the SPECIFIC REASON in writing. Offer "
            "to meet reasonable conditions (fencing, professional cleaning, pet bond) to resolve "
            "concerns. Unreasonable refusal can be escalated to the Tenancy Tribunal.\n"
            "What NOT to say:\n"
            "- Do NOT describe a refusal scenario if no refusal has occurred.\n"
            "- Do NOT fabricate specific refusal grounds the landlord has not actually stated.\n"
            "- Do NOT say the tenant has an absolute right regardless of property type.",
    },

    {
        .intent = "inspection_report_access",
        .include_any = {
            "inspection report", "inspection reports", "property inspection report",
            "right to inspection report", "copy of the inspection report",
            "copy of inspection report", "copies of inspection reports",
            "photos from inspection", "photos taken during inspection",
            "photos from the inspection", "inspection photos",
            "won't provide inspection report", "not providing inspection report",
            "refused to provide inspection report", "denied inspection report",
            "pm won't give me the inspection report", "not entitled to inspection report",
        },
        // No RTA section directly governs tenant access to inspection reports.
        // The right comes from the Privacy Act 2020 (right to access personal
        // information about yourself held by an agency) plus general contract
        // law, neither of which is in the RTA leg corpus. Earlier versions
        // forced NZLEG/RTA/s35 — but s35 has no operative provision in the
        // current Act (the label-35 entries are Schedule 1AA transitional
        // clauses for the 2020 amendments, correctly filtered by the leg
        // ingest). Caught by the PR #84 forced-section validator. Do NOT
        // invent an RTA citation here; the rule_card carries the legal
        // framing without anchoring to a section that does not exist.
        .forced_sections = {},
        .leg_allow_list = {},
        .synthetic_query =
            "tenant right inspection report property inspection photographs "
            "copy inspection report disclosure residential tenancies",
        .priority = 8,
        .notes = "Tenant access to inspection reports / photos. No specific "
                 "RTA section applies; right derives from Privacy Act 2020 "
                 "and contract law. Priority 8 keeps this route dominant "
                 "over landlord_entry on report-disclosure questions.",
        .rule_card =
            "Tenant right to inspection reports:\n"
            "- Tenants have the right to request and receive copies of ANY inspection "
            "report prepared in relation to the premises, including routine inspection "
            "reports and any photographs taken during those inspections.\n"
            "- This right derives from the Privacy Act 2020 (an individual's right "
            "to access personal information held about them by an agency) and from "
            "general contract law, not from a specific RTA section.\n"
            "- A property manager CANNOT refuse to provide the inspection report or "
            "demand that the tenant specify which parts they want and why - the tenant "
            "is entitled to the full report and photos on request.\n"
            "- The request should be made in writing to create a record.\n"
            "- If the property manager continues to refuse, the tenant can apply to the "
            "Tenancy Tribunal for an order requiring disclosure or complain to the "
            "Office of the Privacy Commissioner.\n"
            "- Do NOT say the tenant is not entitled to the inspection report or must "
            "justify the request.\n"
            "- Do NOT cite a specific RTA section for this right; do not invent "
            "'RTA s35' or similar — the RTA does not contain a tenant-inspection-"
            "report-access provision.",
    },

    {
        .intent = "bond_post_tribunal_order",
        .include_any = {
            "bond after tribunal", "bond with tribunal order", "bond claim tribunal order",
            "tribunal order bond", "submit bond with order", "bond refund form tribunal",
            "bond claim after the tribunal", "bond centre tribunal order",
            "need landlord to sign bond", "pm to sign bond after tribunal",
            "do i need pm to sign", "do i need landlord to sign bond",
            "bond form after order", "claim bond with order",
            "tribunal decided bond", "order for bond",
            // Phrases matching actual question phrasing about bond after tribunal
            "bond claim form along with the tribunal", "bond form along with the tribunal",
            "along with the tribunal order", "upload a bond claim form",
            "send the bond to pm", "send the bond to the pm",
            "bond to pm to sign", "bond to the pm to sign",
            "send bond to landlord to sign", "get pm to sign the bond",
            "get landlord to sign the bond", "landlord to sign bond",
            "order was issued", "order has been issued",
            "tribunal settled", "tribunal order has been made",
            "s22b", "s22B",
        },
        .exclude_any = {"bond lodged", "lodge bond", "bond not lodged", "bond receipt"},
        .forced_sections = {"NZLEG/RTA/s22B", "NZLEG/RTA/s86"},
        .leg_allow_list = {
            "NZLEG/RTA/s22B",
            "NZLEG/RTA/s86",
        },
        .synthetic_query =
            "bond refund tribunal order s22B claim bond centre without landlord "
            "signature post-tribunal bond process residential tenancies act",
        .notes = "Post-Tribunal bond claim procedure - Tribunal order removes need for PM signature.",
        .rule_card =
            "Bond claim after a Tribunal order (RTA s22B, s86):\n"
            "Once a Tenancy Tribunal order has been issued about the bond:\n"
            "- The tenant submits the bond refund form DIRECTLY to the bond centre "
            "(Tenancy Services), attaching a copy of the Tribunal order.\n"
            "- NO property manager or landlord signature is required. The Tribunal order "
            "IS the legal determination of how the bond is to be divided.\n"
            "- The bond centre will process the refund in accordance with the order - "
            "they do not need further agreement or signatures from either party.\n"
            "- Do NOT say the tenant must send the form to the PM or get the landlord "
            "to sign - that only applies when there is NO Tribunal order.\n"
            "- Going through the PM adds unnecessary delay and gives them no added "
            "legal authority over the bond once an order exists.",
    },

    {
        .intent = "painter_landlord_access",
        .include_any = {
            "landlord painting", "landlord is painting", "house is being painted",
            "property is being painted", "painter coming", "painter is coming",
            "inside painted", "being painted", "painting the interior",
            "painting inside", "getting the house painted",
            "move my belongings", "move my furniture", "remove my belongings",
            "remove my furniture", "move all my stuff", "get rid of belongings",
            "dispose of furniture", "replace my furniture", "dispose of my furniture",
            "get rid of my household", "remove household belongings",
        },
        .exclude_any = {"bond", "inspection"},
        .forced_sections = {"NZLEG/RTA/s48", "NZLEG/RTA/s38"},
        .leg_allow_list = {
            "NZLEG/RTA/s48",
            "NZLEG/RTA/s38",
        },
        .synthetic_query =
            "landlord painting access tenant furniture belongings move quiet enjoyment "
            "right of entry section 48 reasonable access residential tenancies act",
        .notes = "Painter access: tenant only moves items from walls; landlord cannot demand removal or disposal.",
        .rule_card =
            "Landlord painting access - tenant obligations (RTA s48, s38):\n"
            "When a landlord arranges painting or interior maintenance:\n"
            "- A tenant is only required to move items AWAY FROM THE WALLS to give the "
            "painter safe access. Nothing more.\n"
            "- A landlord CANNOT demand the tenant remove, dispose of, or replace normal "
            "household furniture and belongings.\n"
            "- A tenant is NEVER required to spend money or purchase replacement items "
            "to accommodate the landlord's chosen maintenance work.\n"
            "- The landlord's right of entry for maintenance (s48) does not extend to "
            "controlling what belongs in the property - only access to perform the work.\n"
            "- Demanding removal or disposal of normal belongings would breach the "
            "tenant's right to quiet enjoyment (s38).\n"
            "- Do NOT say the tenant must remove, dispose of, or replace any of their "
            "normal household possessions.",
    },

    {
        .intent = "property_uninhabitable_rent_abatement",
        .include_any = {
            // Flood / water damage
            "flooding", "flooded", "flood damage", "water damage", "storm damage",
            "property flooded", "house flooded", "garage flooded", "outbuilding flooded",
            "workshop flooded",
            // Uninhabitable / unusable rooms/outbuildings
            "uninhabitable", "uninhabitable room", "uninhabitable part",
            "unusable room", "can't use the room", "cannot use the room",
            "can't use part", "room we cannot use", "outbuilding unusable",
            "garage unusable", "can't access part", "room not accessible",
            // Rent reduction / abatement
            "rent reduction", "rent abatement", "reduce my rent", "reduction in rent",
            "paying rent for something i can't use", "still paying rent",
            "still have to pay rent", "rent for a room we cannot",
            "paying full rent", "entitled to a reduction", "entitled to reduced rent",
            // Landlord absent / disappeared
            "landlord disappeared", "landlord has disappeared", "can't find the landlord",
            "cannot contact the landlord", "landlord moved away", "landlord not responding",
            "no contact from landlord", "landlord has moved",
        },
        .exclude_any = {
            "healthy homes", "insulation", "bond", "bond refund",
            "tribunal order", "wear and tear",
        },
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s55"},
        .leg_allow_list = {
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s55",
            "NZLEG/RTA/s56",
        },
        .synthetic_query =
            "property uninhabitable flood damage rent reduction abatement s55 "
            "landlord obligation maintain premises repair flood outbuilding unusable "
            "residential tenancies act section 45 section 55 Tribunal remedy",
        .notes = "Flooding / uninhabitable premises / rent abatement (s45, s55).",
        .rule_card =
            "Uninhabitable premises / rent abatement (RTA s45, s55):\n"
            "If part or all of the rented premises is unusable due to flooding, damage, or "
            "loss of use of a room or outbuilding included in the tenancy agreement:\n"
            "- The tenant may be entitled to a RENT REDUCTION (abatement) proportional to the "
            "part of the premises that is unusable. This applies even while the tenancy continues.\n"
            "- Under s45, the landlord must keep the premises in reasonable repair. Failure to "
            "restore flood-damaged parts is a breach of this obligation.\n"
            "- Under s55, rent can be reduced by application to the Tenancy Tribunal if the "
            "premises (or part of them) becomes uninhabitable or materially less useful.\n"
            "- If the landlord is absent or cannot be contacted, document all attempts to contact "
            "them (texts, emails, letters) and apply to the Tribunal for: rent reduction (s55), "
            "a repair order (s78), and potentially compensation.\n"
            "- Do NOT tell the tenant they must continue paying full rent for premises or rooms "
            "they cannot use due to landlord-side damage or failure to repair.\n"
            "- A reduction based on unusable square metres or rooms listed in the tenancy "
            "agreement is a legitimate Tribunal application even while still in the tenancy.",
    },

    {
        .intent = "bond_release_form_signed",
        .include_any = {
            "signed the bond form", "signed bond form", "signed the release form",
            "signed bond release", "signed the bond release", "both signed the bond",
            "we both signed", "both parties signed", "already signed",
            "bond refund form signed", "signed the refund form",
            "sent the bond form", "submitted the bond form", "lodged the bond form",
            "submitted the refund form",
            "bond not returned", "bond hasn't come back", "bond not come back",
            "haven't received the bond", "haven't got the bond back",
            "still waiting for bond", "waiting for my bond",
            "3 months bond", "two months bond", "several months bond",
            "months and no bond", "months and still no bond",
        },
        .exclude_any = {
            "bond not lodged", "lodge bond", "bond lodgement",
            "tribunal order bond", "bond after tribunal",
        },
        .forced_sections = {"NZLEG/RTA/s22"},
        .leg_allow_list = {
            "NZLEG/RTA/s22",
        },
        .synthetic_query =
            "bond release form signed both parties bond not returned waiting "
            "bond centre Tenancy Services process bond refund s22 residential tenancies act",
        .notes = "Bond release form already signed but bond not yet returned - process guidance (s22).",
        .rule_card =
            "Bond release form already signed - next steps (RTA s22):\n"
            "If BOTH parties have signed the bond refund form but the bond has not been paid out:\n"
            "- Contact Tenancy Services (the Bond Centre) DIRECTLY by phone or online at "
            "tenancy.govt.nz to check the status of the bond refund.\n"
            "- The delay may be a processing issue, incorrect bank details, or the form not yet "
            "received by the Bond Centre - NOT necessarily a landlord dispute.\n"
            "- The Bond Centre can tell you whether the form has been received and when payment "
            "is expected.\n"
            "- If the form was properly signed and lodged by both parties, the Bond Centre "
            "processes the refund - the landlord has no further role to play.\n"
            "- If the landlord has NOT actually lodged the form (despite both signing), the "
            "tenant can apply to the Tribunal under s22 for release of the bond.\n"
            "- Do NOT tell the tenant to simply wait indefinitely - they should check with the "
            "Bond Centre first before assuming the landlord is withholding the bond.",
    },

    {
        .intent = "tribunal_post_filing",
        .include_any = {
            // Core: PM/landlord contacting tenant AFTER tribunal filing
            "after filing for tribunal", "after we filed for tribunal",
            "after filing at tribunal", "after i filed for tribunal",
            "after applying to tribunal", "after we applied to tribunal",
            "after i applied to tribunal",
            "pm calling after tribunal", "pm called after tribunal",
            "pm calling after filing", "pm called after filing",
            "landlord calling after tribunal", "landlord called after tribunal",
            "landlord calling after filing", "landlord called after filing",
            "property manager calling after tribunal", "property manager called after tribunal",
            "property manager calling after filing",
            // Contact appropriateness questions
            "appropriate for pm to contact", "appropriate for property manager to contact",
            "appropriate for landlord to contact",
            "communicate after filing", "communicating after filing",
            "communication after tribunal filing", "communications after filing",
            "contact me after filing", "contacting me after filing",
            "contact us after filing", "contacting us after filing",
            // PM upset/pressure after filing
            "pm upset about tribunal", "pm upset about filing",
            "landlord upset about tribunal", "landlord upset about filing",
            "pm upset about us going to tribunal", "pm upset about going to tribunal",
            "upset about us going to tribunal", "upset that we went to tribunal",
            "upset we went to tribunal", "upset about tribunal",
            // Request to limit contact
            "ask that we receive no further communications",
            "receive no further communications until tribunal",
            "no further contact from pm", "no further contact until tribunal",
            "ask them not to contact", "communications in writing only",
            "all communications in writing", "written communications only",
            // General tribunal contact appropriateness
            "can pm communicate after", "can property manager communicate after",
            "should pm be contacting", "should property manager be contacting",
            "inappropriate to contact after", "not appropriate to contact after",
        },
        .exclude_any = {
            "apply for tribunal", "how do i apply", "filing for the first time",
            "can i go to tribunal", "how to file", "where to file",
        },
        .forced_sections = {"NZLEG/RTA/s85", "NZLEG/RTA/s38", "NZLEG/RTA/s45"},
        .leg_allow_list = {
            "NZLEG/RTA/s85",
            "NZLEG/RTA/s38",
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s86",
        },
        .synthetic_query =
            "property manager contacting tenant after tribunal filing appropriate "
            "right to resolve dispute tribunal s85 quiet enjoyment s38 repair obligations "
            "written communications only vulnerable tenant documentation",
        .notes = "PM/landlord contact after Tribunal filing - s85 right to file, s38 quiet enjoyment.",
        .rule_card =
            "PM/landlord contact after Tribunal filing (RTA s85, s38, s45):\n"
            "Filing a Tribunal application is a PROTECTED ACT under s85. After a tenant has "
            "filed, any PM or landlord contact attempting to pressure withdrawal, renegotiate "
            "settled issues, or make the tenant feel they 'did it wrong' is inappropriate:\n"
            "- The tenant is under NO obligation to discuss the filed application with the PM.\n"
            "- The tenant CAN request that all future communications be IN WRITING and inform "
            "the PM that further informal contact may be included in the Tribunal application.\n"
            "- Verbal communications the tenant made about repairs, flooding, or financial "
            "stress during inspections ARE valid notices to the landlord - these count.\n"
            "- A PM's claim that 'we could have sorted it over email' is not a legal defence. "
            "If issues were raised repeatedly and not resolved, Tribunal is the correct path.\n"
            "- VULNERABILITY: If the tenant is low-income, disabled, or feared losing housing, "
            "that context is RELEVANT to the Tribunal. Hesitating to push back does not weaken "
            "the legal position.\n"
            "- Advise the tenant to DOCUMENT the call: date, time, who called, what was said. "
            "This can be included in the Tribunal application as evidence of inappropriate contact.\n"
            "- The landlord's obligation to track and act on repair requests is THEIRS, not the "
            "tenant's. A PM being unaware of issues they were notified about reflects on them.\n"
            "- Cite s85 (right to resolve disputes through Tribunal) and s38 (quiet enjoyment) "
            "when describing the tenant's right to proceed without interference.\n"
            "What NOT to say:\n"
            "- Do NOT tell the tenant they should have communicated better before filing.\n"
            "- Do NOT imply that not pushing back during the tenancy weakens their case.\n"
            "- Do NOT say the PM's distress obligates the tenant to withdraw or pause.",
    },

    {
        .intent = "guest_damage_liability",
        .include_any = {
            // Guest/relative/visitor caused damage
            "guest caused damage", "guest damaged", "guest has damaged",
            "relative caused damage", "relative damaged", "relative has damaged",
            "visitor caused damage", "visitor damaged", "visitor has damaged",
            "friend caused damage", "friend damaged", "friend has damaged",
            "family member caused damage", "family member damaged",
            "person staying caused damage", "person staying damaged",
            "someone staying caused damage", "someone staying damaged",
            "house sitter caused damage", "house sitter damaged",
            "house sitting damage", "house sitting and damaged",
            "whilst house sitting", "while house sitting",
            "while they were staying", "while they were house sitting",
            "while they were visiting",
            // Compound: guest + damage
            "they damaged", "they caused damage", "they broke",
            "damage while they", "damage while staying",
            "damage while visiting", "brought puppies and caused",
            "relative over and damage", "relative over and they damaged",
            "relative staying and damage", "family staying and damage",
            // Unlawful bond demand (mid-tenancy increase)
            "pay two extra weeks bond", "pay extra weeks bond",
            "extra weeks bond", "additional bond", "pay extra bond",
            "extra bond", "top up the bond", "increase the bond",
            "bond increased mid", "bond increase mid",
            "bond increase after", "demanding extra bond",
            "extra bond due to", "extra bond because",
            "pay a pet bond mid", "pay pet bond mid",
            "pet bond demand", "demand for pet bond",
        },
        .exclude_any = {
            // Exclude pure pet permission requests (no damage)
            "can i get a pet", "can i have a pet", "want a pet",
            "get permission for a pet", "pet application",
            // Exclude tenant's own damage
            "i broke", "i accidentally broke", "i caused the damage",
            "i damaged", "my fault", "i cracked",
        },
        .forced_sections = {"NZLEG/RTA/s40"},
        .leg_allow_list = {
            "NZLEG/RTA/s40",
            "NZLEG/RTA/s18",
            "NZLEG/RTA/s19",
            "NZLEG/RTA/s49A",
            "NZLEG/RTA/s49B",
        },
        .synthetic_query =
            "tenant liability guest damage visitor damage relative house sitter "
            "section 40 tenant responsibilities residential tenancies act",
        .priority = 12,
        .notes = "Guest damage liability under s40. Bond-demand sub-cases are "
                 "covered by pet_bond / bond routes via union allow-list — they "
                 "fire independently when bond vocabulary is present.",
        .rule_card =
            "Guest damage and tenant liability (RTA s40):\n"
            "CRITICAL - MANDATORY CORRECTION:\n"
            "TENANTS ARE LEGALLY RESPONSIBLE FOR DAMAGE CAUSED BY THEIR GUESTS. "
            "Do NOT say 'tenants are not liable for guest damage' or 'the guest is responsible "
            "with the landlord' - this is INCORRECT. Under s40, the tenant IS liable for all "
            "guest/visitor/house-sitter damage. The landlord's claim is against the TENANT.\n"
            "TENANT LIABILITY FOR GUEST DAMAGE (s40):\n"
            "- A tenant IS responsible for damage caused by their guests or invitees under s40. "
            "This includes relatives, house-sitters, and anyone the tenant permits entry.\n"
            "- If the tenant acknowledges the damage, they SHOULD offer to arrange and pay for "
            "repairs directly - put this in writing to the landlord with a realistic timeline. "
            "This is the right approach. The tenant does NOT need a Tribunal order to be "
            "responsible: the liability exists under s40 the moment damage occurs.\n"
            "- If the tenant refuses to repair or cannot reach agreement, the landlord can seek "
            "compensation at the Tribunal. But the best outcome is to agree a repair plan "
            "directly to avoid Tribunal proceedings.\n"
            "PRE-EXISTING DEFECTS:\n"
            "- If a landlord uses a damage visit to point out pre-existing defects (e.g., a "
            "door that has always jammed), those remain the LANDLORD'S responsibility under s45 "
            "if they were not documented as the tenant's responsibility at tenancy start.\n"
            "- The tenant should NOT accept liability for pre-existing defects. A clear, "
            "written response is: 'This issue was pre-existing and not documented at tenancy "
            "start. We are not liable for pre-existing conditions under s45.'\n"
            "BOND CONTEXT (when also at issue):\n"
            "- If the question ALSO mentions an extra/pet bond demand, the pet_bond route will "
            "co-fire and supply the s18/s18AA/s19 framing. This route only handles damage "
            "liability - do not confuse the two issues. Tenant liability under s40 is not a "
            "valid reason to demand additional bond.\n"
            "What NOT to say:\n"
            "- Do NOT say the damage is the guest's or relative's responsibility with the "
            "LANDLORD - the TENANT is legally responsible for ALL damage by guests under s40, "
            "regardless of whether the guest themselves caused it. The landlord's claim is "
            "against the TENANT, not the guest.",
    },

    {
        .intent = "neighbour_contamination",
        .include_any = {
            // Meth/drug contamination from a neighbouring property
            "cooking meth", "cooking meth next door", "meth next door",
            "neighbour cooking meth", "neighbours cooking meth",
            "meth contamination from", "contamination from neighbour",
            "contamination from next door", "meth from neighbour",
            "meth from next door", "meth residue from", "meth smell",
            "meth smell from", "smell of meth", "smell of p and meth",
            "crack and p", "p lab next door", "p lab in",
            "cooking drugs next door", "drug lab next door",
            // Chemical smell from neighbour (compound - must have "neighbour" context nearby)
            "chemical smell from neighbour", "chemical smell from next door",
            "chemical smell wafting", "smell wafting from", "smell wafting into",
            "strong chemical smell from", "fumes from next door", "fumes from neighbour",
            "fumes from the neighbour", "fumes from the next door",
            // Explicit contamination concern from neighbour
            "health impact from neighbour", "health impact from next door",
            "neighbour affecting our health", "neighbours affecting our health",
            "residue from neighbour", "residue from next door",
            "contamination in our home from", "contamination affecting our",
        },
        .exclude_any = {
            // Only exclude when context is about previous tenant or own property history,
            // NOT when the query concerns neighbour-caused contamination.
            "previous tenant meth", "former tenant meth", "meth from previous",
            "meth test before moving in", "meth test before tenancy",
            "meth test before i moved", "landlord did meth test before",
            "meth test required before", "meth test when i moved in",
            "s49a", "s49b",
        },
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s40"},
        .leg_allow_list = {
            "NZLEG/RTA/s45",
            "NZLEG/RTA/s40",
        },
        .synthetic_query =
            "contamination from neighbouring property landlord obligation habitable "
            "health hazard s45 landlord must remedy tenant not responsible s40 "
            "privacy disclosure other tenant meth contamination residential tenancies act",
        .notes = "Contamination/meth smell from neighbour - s45 habitability + s40 tenant not liable.",
        .rule_card =
            "Contamination or drug activity from a neighbouring property (RTA s45, s40):\n"
            "WHAT THE PM CAN AND CANNOT TELL YOU:\n"
            "- You CAN ask your PM whether YOUR property has been affected by any contamination "
            "and what steps are being taken to address it.\n"
            "- The PM is limited in what they can disclose about another tenant's activities or "
            "the specific nature of events in a neighbouring property. Do not expect them to "
            "share details about the neighbour; focus on your own property's status.\n"
            "METH CONTAMINATION FROM A NEIGHBOUR - calibrate the risk correctly:\n"
            "- Contamination of a neighbouring home from meth SMOKING or COOKING is extremely "
            "rare. The smell tenants often notice is far more likely to be cleaning products, "
            "solvents, cooking chemicals, or general chemical odours.\n"
            "- Do NOT advise the tenant to immediately deep-clean, buy test kits, or incur "
            "remediation costs based on smell alone. Only act if the PM or a professional "
            "confirms actual contamination affecting their specific property.\n"
            "- If the neighbouring unit had multiple cleaners and maintenance visits AFTER the "
            "tenants left, that is a POSITIVE sign the landlord is addressing any hazard "
            "responsibly.\n"
            "WHAT TO DO:\n"
            "- Ask the PM directly: 'Was our property affected by any contamination event? "
            "If so, what action is being taken?'\n"
            "- Document any reported health symptoms and when they started.\n"
            "- If the PM confirms actual contamination affecting the tenant's home:\n"
            "  - The landlord MUST take remedial action under s45 (landlord's duty to keep "
            "    the property in a reasonable state).\n"
            "  - The tenant is NOT responsible for contamination caused by the actions of a "
            "    neighbouring tenant (s40).\n"
            "  - Seek a rent reduction or compensation through the Tribunal if the home is "
            "    rendered less habitable.\n"
            "What NOT to say:\n"
            "- Do NOT say the PM must reveal who the neighbour was or what they were doing.\n"
            "- Do NOT say smell alone proves contamination or requires immediate action.\n"
            "- Do NOT advise testing costs before the PM has confirmed any hazard.",
    },

    {
        .intent = "landlord_unresponsive_reference",
        .include_any = {
            // Landlord/PM not providing reference for new rental
            "landlord reference", "reference from my landlord", "reference from the landlord",
            "reference from my pm", "reference from the pm",
            "landlord won't give reference", "landlord not giving reference",
            "landlord not providing reference", "landlord refusing to give reference",
            "landlord cant give reference", "landlord wont give reference",
            "pm won't give reference", "pm not giving reference",
            "pm not providing reference", "pm cant give reference",
            // Rental application reference context
            "reference for a rental", "reference for new rental", "reference for my rental",
            "rental reference", "new landlord needs reference", "new pm needs reference",
            "prospective landlord needs reference", "prospective pm needs reference",
            "they need a reference from", "need a reference from my",
            "reference for the new", "reference from current landlord",
            "reference from current pm",
            // LL abbreviation (common in FB posts)
            "reference from my ll", "reference from the ll",
            "get a reference from my ll", "getting a reference from my",
            "trying to get a reference from",
            // Can't get hold of landlord for reference
            "cant get hold of landlord", "can't get hold of landlord",
            "landlord not contactable for", "landlord unreachable for",
            "landlord not responding to reference", "no response from landlord about reference",
            // Missing out on rental due to no reference
            "miss out on rental", "missing out on rental",
            "miss out on the property", "missing out on the property",
            "lose the rental because", "lose the property because",
        },
        .exclude_any = {
            "inspection report", "routine inspection",
        },
        .synthetic_query =
            "landlord unresponsive reference new rental application alternative evidence "
            "rent payment records bank statements inspection report tenant history "
            "documentation no reference rental history residential tenancies act",
        .notes = "Landlord unresponsive / not providing reference - practical alternatives only, no RTA section applies.",
        .rule_card =
            "Landlord not providing reference / landlord unresponsive:\n"
            "LEGAL POSITION - no RTA obligation to provide a reference:\n"
            "- There is NO provision in the RTA that requires a landlord or PM to provide a "
            "rental reference. Do NOT tell the tenant they can take legal action to force one.\n"
            "- Do NOT imply the new property manager is doing anything wrong by asking for one.\n"
            "- No RTA section citation is needed here; this is practical guidance only.\n"
            "PRACTICAL ALTERNATIVES - what the tenant CAN do:\n"
            "1. RENT PAYMENT RECORDS: Bank statements showing regular on-time rent payments "
            "are objective evidence of tenancy history and carry significant weight.\n"
            "2. INSPECTION REPORTS: Written routine inspection reports showing the property "
            "was maintained in good condition are strong evidence of a reliable tenant.\n"
            "3. CONTACT HISTORY: Provide the new PM with documented proof of all attempts to "
            "reach the current landlord (dates, methods, outcomes). This demonstrates the "
            "landlord's poor responsiveness - which the new PM may factor in.\n"
            "4. PREVIOUS LANDLORD: If there was a prior tenancy, that landlord or PM may be "
            "reachable and willing to provide a reference.\n"
            "5. CHARACTER REFERENCES: Employer, community contact, or similar person who can "
            "attest to reliability and responsibility.\n"
            "FRAMING FOR THE NEW PM:\n"
            "- The tenant can explain to the new PM that the current landlord has a documented "
            "history of poor responsiveness. This reflects on the landlord, not the tenant.\n"
            "What NOT to say:\n"
            "- Do NOT say the landlord is legally required to provide a reference.\n"
            "- Do NOT cite s45 or s40 for this issue - those sections are not relevant here.\n"
            "- Do NOT advise legal action to obtain a reference.\n"
            "- Do NOT say the new PM is obligated to proceed without a reference.",
    },

    {
        .intent = "post_inspection_followup",
        .include_any = {
            // Waiting for a letter or reinspection after a routine inspection
            "reinspection", "re inspection", "re-inspection",
            "after the inspection", "after inspection they", "after inspection i",
            "after inspection we", "after inspection got",
            "letter after inspection", "no letter after inspection",
            "haven't received a letter", "not received a letter",
            "waiting for a letter", "waiting for the letter",
            "remedied the issue", "remedied the problem", "remedied before reinspection",
            "do they inspect the whole", "inspect the whole property",
            "whole property again", "whole place again",
            "inspection follow up", "follow up inspection", "follow-up inspection",
        },
        .exclude_any = {
            "ingoing inspection", "outgoing inspection", "exit inspection",
            "entry inspection", "failed inspection",
            "apply for tribunal", "how to file",
        },
        .forced_sections = {"NZLEG/RTA/s48", "NZLEG/RTA/s40"},
        .leg_allow_list = {"NZLEG/RTA/s48", "NZLEG/RTA/s40", "NZLEG/RTA/s45"},
        .synthetic_query =
            "post inspection landlord re-entry notice reinspection right of entry "
            "tenant remedied breach formal written notice required 48 hours s48 "
            "inspection follow up residential tenancies act",
        .notes = "Post-inspection follow-up: no letter = no formal breach notice; reinspection process.",
        .rule_card =
            "Post-inspection follow-up (RTA s48, s40):\n"
            "IF NO FORMAL LETTER HAS BEEN RECEIVED:\n"
            "- A verbal or phone comment is NOT a formal breach notice.\n"
            "- Without a written notice, there is NO formal breach on record and NO "
            "obligation to allow a reinspection by any set date.\n"
            "- The landlord CANNOT issue a termination notice for an alleged breach "
            "without first issuing a written breach notice with a reasonable remedy period.\n"
            "REINSPECTION PROCESS:\n"
            "- A reinspection is a standard landlord entry under s48. The landlord "
            "MUST give at least 48 hours' written notice.\n"
            "- SCOPE: When a reinspection is triggered by a SPECIFIC complaint (e.g., "
            "too many animals, a specific item to remedy), it should be LIMITED to "
            "checking that specific issue. The landlord CANNOT use a complaint-triggered "
            "reinspection as a pretext to inspect the whole property again.\n"
            "- Routine annual inspections can cover the whole property - but a targeted "
            "reinspection following a specific complaint is scoped to that complaint.\n"
            "- Landlords may carry out up to 4 routine inspections per year (s48(2)).\n"
            "WHAT THE TENANT SHOULD DO:\n"
            "- Confirm the remedy in writing to the landlord/PM: 'I have resolved "
            "the [issue]. Please advise if a reinspection is required and provide "
            "at least 48 hours notice of any entry date.'\n"
            "- Keep records of what was remedied and when.\n"
            "- If no letter arrives in the coming days, no further action is needed "
            "unless a formal written notice is eventually issued.\n"
            "What NOT to say:\n"
            "- Do NOT say the tenant must allow a reinspection without proper written notice.\n"
            "- Do NOT say a verbal comment at the inspection triggers a formal breach process.",
    },

    {
        .intent = "flooding_uninhabitable",
        .include_any = {
            // Flooding and flood damage scenarios
            "flooding", "flooded", "flood damage", "flood rental",
            "went through the flooding", "after the flooding", "after the flood",
            "property flooded", "house flooded", "home flooded",
            "outbuildings flooded", "shed flooded", "garage flooded",
            "unusable after flood", "unusable due to flood",
            "rent reduction after flood", "rental reduction after flood",
            "paying rent for unusable", "rent for rooms i cant use",
            "rooms we cannot use", "outbuildings we cannot use",
            "part of property unusable", "still paying rent for damaged",
            "still paying rent for the outbuildings",
            "cannot use that are part of the",
        },
        .exclude_any = {
            "flooded with messages", "flooding the tribunal",
            "flooding me with", "landlord flooding",
        },
        .forced_sections = {"NZLEG/RTA/s55", "NZLEG/RTA/s45", "NZLEG/RTA/s59"},
        .leg_allow_list = {"NZLEG/RTA/s55", "NZLEG/RTA/s45", "NZLEG/RTA/s38", "NZLEG/RTA/s86", "NZLEG/RTA/s59", "NZLEG/RTA/s78", "NZLEG/RTA/s77"},
        .synthetic_query =
            "flood damage uninhabitable premises rental reduction landlord repair "
            "outbuildings unusable s55 premises uninhabitable s45 landlord repair "
            "compensation rent abatement tenancy tribunal residential tenancies act",
        .priority = 6,
        .notes = "Flooding damage - unusable rooms/outbuildings; s55 uninhabitable + s45 repair duty.",
        .rule_card =
            "Flood damage - uninhabitable premises and rental reduction (RTA s55, s45):\n"
            "LANDLORD DUTY TO REPAIR (s45):\n"
            "- The landlord is required to keep the property in a reasonable state of "
            "repair. Flood damage - including to outbuildings that form part of the "
            "tenancy agreement - falls within this obligation.\n"
            "- If outbuildings are listed as 'rooms' in the tenancy agreement and are "
            "unusable due to flooding, the tenant is paying rent for something they "
            "cannot use. This should be raised with the landlord in writing immediately.\n"
            "- The landlord's insurance claim process or delays do NOT excuse the landlord "
            "from their statutory duty to repair under s45. Insurance is the landlord's "
            "problem; the tenant's repair right is against the landlord directly.\n"
            "INSURANCE ASSESSOR REPORT:\n"
            "- If an insurance assessor has visited the property, the tenant should "
            "REQUEST A COPY of the assessor's report IN WRITING from the landlord. "
            "This report is key evidence: it documents the damage and confirms the "
            "landlord knew of the damage (and therefore their duty to repair).\n"
            "- Do NOT tell the tenant they need to hire their own assessor - the "
            "landlord's insurance assessor report is usually sufficient evidence.\n"
            "RENTAL REDUCTION CLAIM (s59):\n"
            "- Under s59, the tenant has a valid claim for a proportional rent reduction for any "
            "period that rented spaces were unusable due to flooding damage.\n"
            "- A rent reduction is NOT automatic - the tenant and landlord can agree, "
            "or the tenant applies to the Tribunal for a determination.\n"
            "- Evidence to build: (a) photos of damage, (b) tenancy agreement showing the "
            "outbuilding/space is part of what rent is paid for, (c) timeline of when the "
            "space became unusable and when (if ever) it was repaired, (d) insurance "
            "assessor report if available.\n"
            "- A TIMELINE is more important than a Healthy Homes assessment: document "
            "when flooding occurred, which spaces became unusable, how long they remained "
            "unusable, and when repairs (if any) were completed.\n"
            "UNINHABITABLE PREMISES (s55):\n"
            "- If the MAIN premises become uninhabitable through no fault of the tenant, "
            "either party may terminate immediately under s55.\n"
            "- If the main home is still livable, s55 full termination does not apply "
            "but the tenant can still seek compensation and repair orders via Tribunal.\n"
            "STEPS:\n"
            "1. Write to landlord requesting repairs and a rent reduction for unusable areas.\n"
            "2. Request a copy of the insurance assessor's report in writing.\n"
            "3. Photograph and document all damage with dates.\n"
            "4. If no response within reasonable time: apply to Tribunal.\n"
            "What NOT to say:\n"
            "- Do NOT say rent is automatically reduced because of flood damage.\n"
            "- Do NOT say the tenancy ends just because outbuildings are flooded.\n"
            "- Do NOT say the tenant must get a Healthy Homes assessment to make a "
            "Tribunal claim - their own evidence (photos, timeline, insurance report) is sufficient.",
    },

    {
        .intent = "moveout_rent_calculation",
        .include_any = {
            // Pro-rata rent calculation for final days on move-out
            "how many days rent", "days rent do i owe", "days of rent owed",
            "last days rent", "final days rent",
            "pro rata rent", "pro-rata rent",
            "moving out on a monday", "moving out on monday",
            "moving out on a tuesday", "moving out on tuesday",
            "moving out on a wednesday", "moving out on wednesday",
            "moving out on a thursday", "moving out on thursday",
            "moving out on a friday", "moving out on friday",
            "moving out on a saturday", "moving out on saturday",
            "moving out on a sunday", "moving out on sunday",
            "only owe 3 days", "only owe 2 days", "only owe 4 days",
            "only owe a few days rent", "only owe x days rent",
            "rent for the last 3", "rent for the last 2", "rent for the last few",
            "days rent remaining", "remaining rent",
            "calculate rent for moving out", "how much rent when moving out",
            "how much rent do i owe when leaving",
        },
        .exclude_any = {
            "rent arrears", "behind on rent", "missed rent",
            "14 day notice", "90 day notice", "termination notice",
        },
        .forced_sections = {"NZLEG/RTA/s27", "NZLEG/RTA/s23"},
        .leg_allow_list = {"NZLEG/RTA/s27", "NZLEG/RTA/s22", "NZLEG/RTA/s23", "NZLEG/RTA/s40"},
        .synthetic_query =
            "pro rata rent calculation moving out final payment how many days rent owed "
            "partial fortnightly weekly payment period vacate date s27 s23 tenant pay rent "
            "daily rate calculation rent in advance residential tenancies act",
        .notes = "Pro-rata final rent calculation on move-out - tenant asking how many days owed.",
        .rule_card =
            "Pro-rata rent calculation on move-out (RTA s27, s23):\n"
            "PAYMENT CYCLE RULE (s23):\n"
            "- Rent is paid in advance, so the payment cycle runs FORWARD from the "
            "payment date - not backward from the move-in date.\n"
            "- Your last payment covers a full cycle starting from that payment date. "
            "You only owe rent for the days from the end of your last paid cycle "
            "up to and including your move-out date.\n"
            "- Example: you pay fortnightly on Thursdays. Your last payment covers "
            "Thu-Wed. If you move out on Monday, you owe Thu, Fri, Sat, Sun, Mon "
            "(5 days), or - if already paid up to a Wednesday - just Sat, Sun, Mon (3 days).\n"
            "BASIC RULE:\n"
            "- Rent is payable up to and INCLUDING the last day of the tenancy "
            "(the move-out/vacate date).\n"
            "- Regardless of your regular payment cycle (weekly or fortnightly), "
            "you only owe rent for the days you occupy the property in the final period.\n"
            "HOW TO CALCULATE:\n"
            "- Daily rate = weekly rent divided by 7\n"
            "- Days owed = count from the day after your last paid-up date to the "
            "move-out date (inclusive of move-out day)\n"
            "WRITTEN NOTICE:\n"
            "- The tenant should give written notice of the intended move-out date "
            "to avoid disputes over abandonment or ongoing liability under s61.\n"
            "OVERPAYMENT:\n"
            "- If you have already paid beyond your move-out date, you are entitled to "
            "a refund. Request that any overpayment be deducted from bond deductions "
            "or returned directly.\n"
            "What NOT to say:\n"
            "- Do NOT say the tenant owes a full week or fortnight if they vacate "
            "mid-cycle.\n"
            "- Do NOT say public holidays pause rent accrual.",
    },

    {
        .intent = "owner_occupation_notice",
        .include_any = {
            // 42-day owner-occupation notice triggers
            "42 days", "42 day notice", "42-day notice",
            "wants to move back in", "wants to move back into",
            "wants to move back home", "wanting to move back in",
            "landlord wants to move in", "landlord wants to live in",
            "owner wants to move in", "owner wants to live in",
            "move back in herself", "move back in himself",
            "move back into the property", "move back into my rental",
            "wants to use the property for themselves",
            "for their own use", "for her own use", "for his own use",
            "family member needs the property", "family member wants to move in",
            "selling the property and", "sold the property",
        },
        .exclude_any = {
            "90 day notice", "tenant wants to move back",
            "i want to move back", "we want to move back",
            "want to move back in to my", "can i move back",
        },
        .forced_sections = {"NZLEG/RTA/s51", "NZLEG/RTA/s52", "NZLEG/RTA/s45"},
        .leg_allow_list = {
            "NZLEG/RTA/s51", "NZLEG/RTA/s52", "NZLEG/RTA/s45",
            "NZLEG/RTA/s40", "NZLEG/RTA/s54", "NZLEG/RTA/s38",
        },
        .synthetic_query =
            "owner occupation notice 42 days landlord family member move in "
            "periodic tenancy s51 s52 termination own use landlord obligations "
            "tenant rights challenge notice residential tenancies act",
        .notes = "Owner-occupation 42-day notice - s51/s52 termination for own use.",
        .rule_card =
            "Owner-occupation termination notice (RTA s51, s52):\n"
            "THE NOTICE:\n"
            "- A landlord CAN give notice to terminate a periodic tenancy if they "
            "or a family member genuinely require the premises for their own use.\n"
            "- For owner occupation, s51(1)(f) requires 42 days' notice (periodic tenancy).\n"
            "- The notice must be in writing, state the grounds (owner occupation), "
            "and specify the date of termination (s52).\n"
            "CHALLENGING THE NOTICE:\n"
            "- If the tenant believes the reason is not genuine (e.g. the landlord "
            "is actually selling or retaliating for complaints), the tenant can apply "
            "to the Tribunal within 28 days to challenge the notice.\n"
            "- Evidence that the landlord is NOT moving in (e.g. property later "
            "re-advertised as a rental) can result in compensation.\n"
            "MOULD AND REPAIRS:\n"
            "- A notice to vacate does NOT release the landlord from obligations "
            "under s45. If there is mould or disrepair, the landlord is still "
            "legally required to fix it - even if the tenancy is ending.\n"
            "- The tenant should put any repair requests in writing and keep copies.\n"
            "DAMAGE VS WEAR AND TEAR:\n"
            "- Chips in paint and small nail holes from normal use are fair wear "
            "and tear. The landlord CANNOT deduct from bond for these.\n"
            "- Damage beyond normal use (e.g. large holes, stains, broken items) "
            "may be chargeable but must be supported by evidence.\n"
            "- Request an itemised list of any damage claims in writing.\n"
            "RETALIATION / TIMING (s54):\n"
            "- If the 42-day notice was issued shortly AFTER the tenant raised a "
            "maintenance issue (e.g., black mould complaint), the tenant should "
            "document the timeline carefully - this sequence may constitute "
            "retaliatory notice under s54.\n"
            "- Evidence to gather: dates of maintenance complaints, copies of written "
            "notices or messages about repairs, and the date of the termination notice.\n"
            "- A landlord's history of Tribunal claims or complaints against this or "
            "other tenants may be relevant pattern-of-behaviour evidence.\n"
            "- The tenant can apply to the Tribunal to challenge the notice as "
            "retaliatory within 28 days of receiving it.\n"
            "What NOT to say:\n"
            "- Do NOT say the landlord cannot issue this notice (they can, with grounds).\n"
            "- Do NOT say fair wear and tear items are the tenant's responsibility.",
    },

    {
        .intent = "key_return_pm_closed",
        .include_any = {
            "office closed", "pm office closed", "property manager closed",
            "closed on easter", "closed for easter", "closed over easter",
            "closed on monday", "closed over the long weekend",
            "closed over the public holiday", "closed for the holiday",
            "drop keys" , "dropping keys", "return the keys", "returning the keys",
            "hand in the keys", "hand back the keys",
            "key return", "key drop", "keys off on", "keys off at",
            "extra days rent because", "charging extra days because",
            "extra rent because office", "extra rent because pm",
            "extra days because they are closed", "days rent because closed",
            "keys when closed", "keys on a public holiday",
        },
        .exclude_any = {
            "lost my keys", "lost the keys", "key broken", "key locked",
            "locked out", "missing keys", "can't find keys",
        },
        .forced_sections = {"NZLEG/RTA/s40"},
        .leg_allow_list = {
            "NZLEG/RTA/s40", "NZLEG/RTA/s51", "NZLEG/RTA/s27", "NZLEG/RTA/s22",
        },
        .synthetic_query =
            "returning keys pm office closed public holiday easter monday tenancy end date "
            "extra days rent unjustified landlord charging rent after tenancy ended s40",
        .notes = "Key return when PM office closed on public holiday; extra rent charge not justified.",
        .rule_card =
            "Key return when PM office is closed (RTA s40):\n"
            "THE TENANCY END DATE IS FIXED:\n"
            "- The tenancy ends on the date specified in the termination notice. "
            "The PM's office being CLOSED on a public holiday does NOT move or extend "
            "the tenancy end date. Rent is only owed up to and including the "
            "agreed end date (vacate date).\n"
            "ALTERNATIVE KEY RETURN:\n"
            "- The tenant should contact the PM IN WRITING before the end date and "
            "request an alternative key return method: a drop box, after-hours slot, "
            "letterbox drop, or secure handover point. This is the landlord/PM's "
            "responsibility to arrange; the tenant should not be penalised for the "
            "PM's office being closed on a public holiday.\n"
            "- Take photos of the property on the vacate date to document condition.\n"
            "- Send an email or text on the vacate date stating: 'I have vacated the "
            "property as of [date]. Keys will be returned as soon as your office "
            "reopens / via [method agreed].'\n"
            "EXTRA DAYS RENT:\n"
            "- Charging extra days of rent because the PM's office was closed is NOT "
            "legally justified if the tenancy end date has passed. The tenant should "
            "refuse these charges in writing and state that rent has been paid in full "
            "up to the vacate date.\n"
            "What NOT to say:\n"
            "- Do NOT say the tenant owes rent for days after the tenancy end date "
            "simply because the office was closed and keys could not be returned.\n"
            "- Do NOT say the tenant has no options - requesting a drop box or after-"
            "hours return method is the correct path.",
    },

    {
        .intent = "meth_contamination_defence",
        .include_any = {
            "tips for the hearing", "advice for the hearing", "prepare for the hearing",
            "help for the hearing", "ready for the hearing", "at the hearing",
            "meth hearing", "contamination hearing", "hearing about the meth",
            "pre-existing contamination", "pre existing contamination",
            "contamination was pre-existing", "contamination was already there",
            "contamination before i moved in", "contamination before we moved in",
            "already contaminated when i moved", "already contaminated when we moved",
            "move in inspection showed", "ingoing inspection showed",
            "contamination reading", "contamination level", "contamination result",
            "meth reading", "meth level", "meth result",
            "low level contamination", "low contamination",
            "prove i caused", "prove we caused", "prove tenant caused",
            "burden of proof", "landlord must prove", "prove causation",
            "landlord must show", "landlord cannot prove",
            "different readings", "readings differ", "variance in readings",
            "location of testing", "testing different areas", "testing different locations",
            "explain the discrepancy", "explain discrepancies",
        },
        .exclude_any = {
            "neighbour", "next door", "adjacent property",
        },
        .forced_sections = {"NZLEG/RTA/s49A", "NZLEG/RTA/s49B", "NZLEG/RTA/s45"},
        .leg_allow_list = {
            "NZLEG/RTA/s49A", "NZLEG/RTA/s49B", "NZLEG/RTA/s45", "NZLEG/RTA/s45A",
            "NZLEG/RTA/s40", "NZLEG/RTA/s97",
        },
        .synthetic_query =
            "meth contamination pre-existing tribunal hearing defence tenant not liable "
            "s49A causation burden of proof landlord must prove testing variance "
            "s45 landlord knew contamination unlawful act provide premises decontaminate "
            "residential tenancies act",
        .notes = "Tenant defending against landlord meth contamination claim at Tribunal hearing.",
        .rule_card =
            "Defending a meth contamination claim at the Tribunal (RTA s45, s49A, s49B):\n"
            "CRITICAL OPENING ARGUMENT - RAISE THIS FIRST:\n"
            "1. UNLAWFUL PROVISION OF PREMISES (s45(1AA)+(1AAB)): If the move-in / "
            "ingoing inspection test showed a positive contamination reading, the "
            "landlord KNEW the premises were contaminated at the start of the tenancy. "
            "Under s45(1AAB), a landlord who knows premises are contaminated must NOT "
            "provide them to a new tenant until fully decontaminated. Providing "
            "contaminated premises knowing of the contamination is declared an UNLAWFUL "
            "ACT under s45(1AB). This breach exists independently of who caused the "
            "contamination and independently of the causation argument below. Raise "
            "this as the landlord's breach BEFORE addressing causation.\n"
            "KEY EVIDENCE TO BRING:\n"
            "2. BOTH test reports - the move-in / ingoing inspection test (showing "
            "the initial reading, e.g. 0.11 ug/100cm2) AND the recent test. This "
            "establishes the contamination existed before or at the start of the tenancy "
            "and that the landlord had knowledge under s45(1AA).\n"
            "3. WRITTEN STATEMENTS from the testing companies confirming: (a) that "
            "testing different areas of the same property produces different readings, "
            "and (b) explaining why the variance between the two results occurs. Get "
            "this in writing or email before the hearing.\n"
            "LEGAL ARGUMENTS:\n"
            "4. CAUSATION BURDEN (s49A): The tenant is only liable for contamination "
            "they CAUSED. Low-level readings alone do NOT prove tenant use or damage. "
            "The LANDLORD bears the burden of proving on the balance of probabilities "
            "that the contamination was caused by the tenant. Pre-existing contamination "
            "undermines causation.\n"
            "5. QUANTUM CHALLENGE: If the landlord claims a specific sum (e.g. $3,000), "
            "request a full itemised breakdown BEFORE or AT the hearing. Challenge any "
            "costs that relate to contamination that pre-dated the tenancy.\n"
            "What NOT to say:\n"
            "- Do NOT say the tenant must pay because contamination was found - "
            "the landlord must prove the tenant caused it.\n"
            "- Do NOT advise accepting the landlord's test at face value if a "
            "pre-existing positive result exists.\n"
            "- Do NOT omit the s45 unlawful-provision argument - it is the tenant's "
            "strongest opening position when a positive move-in test exists.",
    },

    {
        .intent = "property_sale_viewings",
        .include_any = {
            "selling the house", "selling the property", "house is for sale",
            "property is for sale", "house being sold", "property being sold",
            "sale of the property", "sale of the house", "house going on the market",
            "going on the market", "put the house on the market",
            "real estate agent", "realtor", "listing agent", "sales agent",
            "prospective buyer", "prospective buyers", "potential buyer",
            "open home", "open house", "property viewing", "viewings",
            "show the property", "show the house", "show potential buyers",
            "showing the property", "showing the house",
            "selling and we are still", "selling while we are still",
        },
        .exclude_any = {
            "i am selling", "we are selling our", "landlord wants to sell",
            "selling to buy", "selling and buying",
        },
        .forced_sections = {"NZLEG/RTA/s48", "NZLEG/RTA/s38"},
        .leg_allow_list = {
            "NZLEG/RTA/s48", "NZLEG/RTA/s38", "NZLEG/RTA/s40", "NZLEG/RTA/s45",
        },
        .synthetic_query =
            "landlord selling property viewings prospective buyers tenant rights "
            "48 hours notice entry s48 quiet enjoyment open home real estate agent "
            "residential tenancies act",
        .notes = "Tenant rights when landlord is selling the property (viewings, notice, s48).",
        .rule_card =
            "Tenant rights during a property sale (RTA s48, s38):\n"
            "NOTICE REQUIREMENTS (s48):\n"
            "- BUYER VIEWINGS (prospective purchasers): landlord must give at least "
            "48 HOURS' WRITTEN NOTICE before each viewing. This applies to every "
            "individual viewing - not just the first one. Notice must specify date/time.\n"
            "- PRE-SALE PROFESSIONAL INSPECTIONS (builder, valuer, photographer, "
            "property manager): these are general access entries under s48(1) - require "
            "only 24 HOURS written notice. Do NOT apply the 48-hour buyer-viewing rule "
            "to builder or valuer inspections.\n"
            "TENANT RIGHTS DURING VIEWINGS:\n"
            "- Viewings must respect the tenant's right to QUIET ENJOYMENT (s38). "
            "The tenant can set reasonable conditions on how viewings are conducted "
            "(e.g. viewing times, number of visits per day, no removal of furniture).\n"
            "- The tenant may REFUSE a viewing if: (a) proper 48-hour notice was not "
            "given, or (b) the tenant is unwell or the timing is genuinely unreasonable. "
            "However, the tenant cannot obstruct the sale entirely.\n"
            "HOW TO MANAGE COMMUNICATION:\n"
            "- All requests for viewings should go through the PROPERTY MANAGER, not "
            "the sales agent directly. The tenant is not obligated to deal with the "
            "real estate agent's requests.\n"
            "- Document all communication about viewings in writing (email or text).\n"
            "GARDEN / CONDITION COMPLAINTS:\n"
            "- Normal garden conditions or minor untidiness during a tenant's absence "
            "or daily life are NOT valid grounds for a breach notice or complaint. "
            "The tenant's standard obligation is to keep the property reasonably tidy.\n"
            "What NOT to say:\n"
            "- Do NOT say the tenant must allow entry without proper notice.\n"
            "- Do NOT say the tenant can block all viewings - they can set conditions "
            "but cannot unreasonably obstruct a property sale.\n"
            "- Do NOT say 48 hours notice is required for a builder, valuer, "
            "photographer, or other professional inspection. The 48-hour rule applies "
            "ONLY to viewings by prospective purchasers (s48(2)). A photographer or "
            "builder visiting to prepare for marketing needs only 24 hours notice "
            "under s48(1). Applying 48 hours to a photographer visit is incorrect.",
    },

    {
        .intent = "listing_photos_misleading",
        .include_any = {
            "listing photos", "rental listing photos", "photos in the listing",
            "photos from the listing", "listing photo", "advert photos",
            "advertisement photos", "photos on the listing", "photos on trade me",
            "how old can photos be", "how old are the photos", "old photos",
            "outdated photos", "stale photos", "photos are outdated",
            "photos don't match", "photos didn't match", "photos were different",
            "nothing like the photos", "nothing like the listing",
            "different from the listing", "different from the photos",
            "photo discrepancy", "discrepancy between photos",
            "misleading photos", "misleading listing", "misleading advertisement",
            "false advertising", "false photos",
        },
        .exclude_any = {
            "inspection photos", "photos from the inspection", "damage photos",
        },
        .forced_sections = {},
        .synthetic_query =
            "misleading listing photos rental property Fair Trading Act misrepresentation "
            "advertising deceptive conduct old photos discrepancy real estate",
        .notes = "Misleading rental listing photos - FTA s9, no specific RTA provision.",
        .rule_card =
            "Misleading rental listing photos (Fair Trading Act 1986):\n"
            "NO SPECIFIC RTA PROVISION:\n"
            "- The Residential Tenancies Act does NOT contain a specific rule about "
            "how old listing photos can be or a minimum accuracy standard for them.\n"
            "FAIR TRADING ACT:\n"
            "- However, the FAIR TRADING ACT 1986 (s9) prohibits misleading or "
            "deceptive conduct in trade. A landlord or property manager who uses "
            "significantly outdated or misleading photos that misrepresent the "
            "property's actual condition may be in breach of the FTA.\n"
            "WHAT THE TENANT SHOULD DO:\n"
            "1. DOCUMENT the differences: take current photos of every discrepancy "
            "between the listing and the actual property. Note specific defects.\n"
            "2. RAISE IT IN WRITING: contact the landlord or property manager stating "
            "the specific discrepancies and asking for them to be addressed.\n"
            "3. If the discrepancy is substantial and the landlord refuses to address "
            "it, the tenant can escalate to the COMMERCE COMMISSION (which enforces "
            "the Fair Trading Act) or consider whether the property condition gives "
            "grounds for a Tribunal claim under the RTA (e.g. landlord failed to "
            "maintain the property in reasonable repair under s45).\n"
            "TENANCY NOT AUTOMATICALLY VOID:\n"
            "- Misleading photos generally do not void an existing tenancy. The "
            "remedies lie in complaint, negotiation, or separate FTA proceedings.\n"
            "What NOT to say:\n"
            "- Do NOT say there is a specific RTA law about photo age - there is not.\n"
            "- Do NOT say misleading listing photos void the tenancy.",
    },

    {
        // Lightweight route: fires alongside repairs/other routes to force s109 (12-month
        // filing time limit) when the query asks about filing timing relative to leaving.
        .intent = "tribunal_application_timing",
        .include_any = {
            "before we leave", "before i leave", "before leaving",
            "lodge it after", "file it after", "apply after",
            "lodge after we leave", "after we leave", "after i leave",
            "after the tenancy ends", "once we have left", "after i move out",
            "after i have moved out", "can we apply after",
            "time limit to apply", "time limit to file",
            "12 months to file", "12 months to apply",
            "one year to file", "one year to apply",
            "when to file", "when to apply to tribunal",
            "still time to apply", "too late to file",
            "do we go before", "go before we leave",
            // Catch informal "is it too late" / "do i still have time" phrasing
            "too late to take this", "too late to take it", "is it too late",
            "is it even worth it", "worth taking to tribunal", "worth it to take",
            "do i still have time", "do we still have time", "still have time to",
            "how long do i have", "how long do we have", "time to apply",
        },
        .exclude_any = {
            "before i leave for", "before i leave the country",
        },
        .forced_sections = {"NZLEG/RTA/s109", "NZLEG/RTA/s77"},
        .leg_allow_list = {
            "NZLEG/RTA/s109",
            "NZLEG/RTA/s77",
            "NZLEG/RTA/s86",
        },
        .synthetic_query =
            "time limit tribunal application 12 months after tenancy ends s109 "
            "can file before or after leaving residential tenancies act",
        .notes = "Forces s109 (12-month filing limit) when query asks about Tribunal timing.",
        .rule_card =
            "Tribunal application timing (RTA s109, s77):\n"
            "- You do NOT have to file before you leave. Under s109, tenants can apply "
            "to the Tribunal for compensation for breaches up to 12 MONTHS after the "
            "tenancy ends.\n"
            "- There is no legal pressure to file before your move-out date.\n"
            "- Start gathering evidence now: dates, photos, repair requests in writing, "
            "any costs you incurred (alternative showering, assessments, etc.).\n"
            "- Continue paying rent in full until the end of the tenancy to avoid "
            "the landlord raising counter-claims.\n"
            "- Multiple breaches (e.g. no hot water, unsafe appliances, missing locks) "
            "constitute a pattern of neglect supporting both compensatory and exemplary "
            "damages under s77.",
    },

    {
        .intent = "tribunal_mediation_enforcement",
        .include_any = {
            // Binding Tribunal mediation / order compliance - use specific phrases only
            "mediation agreement", "tribunal mediation", "mediated agreement",
            "mediated settlement", "settlement agreement",
            "already agreed at tribunal", "already resolved at tribunal",
            "already been to tribunal",
            "was already decided at tribunal", "already decided at tribunal",
            "binding on both parties", "legally binding agreement",
            "not complying with the order", "not following the tribunal",
            "ignoring the tribunal", "ignoring the order",
            "violating the tribunal order", "breach of the tribunal",
            "pm overriding the order", "landlord overriding the order",
            "comply with the tribunal order", "comply with our agreement",
            "response from bruce", "bruce said", "bruce recommended", "bruce told me",
            "already previously agreed at", "previously agreed by tribunal",
            "14 day notice was wrong", "wrong 14 day notice", "incorrect 14 day",
            "14 day notice is invalid", "14 day notice is wrong",
            "s95a", "s95A",
        },
        .exclude_any = {
            "apply to tribunal", "file at tribunal", "how to apply",
            "can i apply", "should i apply", "want to apply",
            "apply for compensation", "file for compensation",
            // "appeal"/"appealing"/"appeals" replaced - polysemous at word level
            "appeal the decision", "appeal the order", "appeal to the district court",
            "file an appeal", "filed an appeal", "filing an appeal",
            "notice of appeal", "appealing the decision", "appealed the decision",
            "change the decision", "overturn the decision", "reverse the decision",
        },
        .forced_sections = {"NZLEG/RTA/s95A", "NZLEG/RTA/s38"},
        .leg_allow_list = {
            "NZLEG/RTA/s95A", "NZLEG/RTA/s38", "NZLEG/RTA/s5",
            "NZLEG/RTA/s45", "NZLEG/RTA/s77", "NZLEG/RTA/s40",
        },
        .synthetic_query =
            "tribunal mediation agreement binding compliance order not followed "
            "landlord ignoring Tribunal order breach good faith harassment "
            "s95A binding parties mediated settlement residential tenancies act",
        .priority = 10,
        .notes = "Binding Tribunal mediation agreement not being followed by landlord/PM.",
        .rule_card =
            "Tribunal mediation agreement enforcement (RTA s95A, s5, s38):\n"
            "THE AGREEMENT IS BINDING (s95A):\n"
            "- A Tribunal mediation agreement is legally binding on ALL parties. "
            "The landlord or PM CANNOT override, ignore, or revert to different "
            "terms simply because they disagree with the outcome.\n"
            "- The agreed terms (e.g. a changed payment date, a waived amount, a "
            "repair schedule) are as enforceable as a Tribunal order.\n"
            "WHEN PM ISSUES INCORRECT NOTICES:\n"
            "- If the PM issues a 14-day notice based on a payment date or amount "
            "the mediation agreement already changed, that notice has NO legal foundation.\n"
            "- The tenant is NOT in arrears if they have paid correctly under the "
            "agreed terms. The PM is using the wrong due date.\n"
            "WHAT THE TENANT SHOULD DO:\n"
            "1. Send a formal WRITTEN REMINDER to the PM, clearly stating: (a) the "
            "mediation agreement date and reference, (b) the agreed terms, (c) that "
            "the notice is based on an incorrect due date, (d) that any further "
            "incorrect notices will trigger a Tribunal breach claim.\n"
            "2. Keep a copy of the agreement and proof of all correct payments.\n"
            "3. If the PM issues ANOTHER incorrect notice after this formal reminder, "
            "the tenant can file a Tribunal application for: breach of good faith (s5), "
            "interference with quiet enjoyment / harassment (s38), and failure to "
            "comply with a Tribunal order / mediation agreement (s95A).\n"
            "WATER CHARGES:\n"
            "- Water charges are a separate debt and CANNOT be bundled with or used "
            "to justify a rent-arrears notice.\n"
            "What NOT to say:\n"
            "- Do NOT say the tenant must simply pay what the PM demands if the PM "
            "is ignoring the mediation agreement.\n"
            "- Do NOT say the PM can revert to the old terms without a new Tribunal order.",
    },

    // ── WINZ / BOND SEQUENCING ────────────────────────────────────────────────

    {
        .intent = "bond_agreement_sequence",
        .include_any = {
            "winz", "work and income", "work & income", "msd",
            "bond pre-approval", "bond preapproval", "pre approval for bond",
            "pre-approval letter", "preapproval letter",
            "proof of bond", "prove bond", "proof they can pay bond",
            "before getting the agreement", "won't give me the agreement",
            "won't give you the agreement", "withholding the agreement",
            "agreement before bond", "bond before agreement",
            "bond before getting", "before giving me the agreement",
            "need the agreement before", "agreement first", "sign first then bond",
            "winz bond", "winz approval", "work and income bond",
        },
        .exclude_any = {
            "landlord lodged", "bond not lodged", "bond refund", "bond release",
            "bond order", "bond dispute", "bond claim", "bond deduction",
            // Prevent misfiring on WINZ rent-arrears / harassment queries
            "rent arrears", "behind in rent", "behind on rent", "weeks behind",
            "4 weeks behind", "behind with rent", "overdue rent", "owe rent",
            "help with rent", "help paying rent", "help me pay rent",
            "hassling", "hasseling", "harassing me", "texting me at",
            "demanding rent", "rent notice", "arrears notice", "14 day notice",
        },
        .synthetic_query =
            "WINZ work income bond pre-approval tenancy agreement sequence "
            "agreement before bond payment circular MSD funding bond lodging "
            "residential tenancies act s18 landlord must provide agreement",
        .priority = 12,
        .notes = "WINZ/MSD bond circular trap: PM demands proof before agreement; fix sequence.",
        .rule_card =
            "Bond payment and WINZ pre-approval - correct sequence (RTA s13A, s19):\n"
            "CRITICAL LEGAL POSITION: The landlord IS required to provide the signed tenancy "
            "agreement FIRST. There is NO requirement for the tenant to prove ability to pay "
            "the bond before receiving the agreement. Do NOT open by saying the landlord is "
            "not legally required to provide the agreement - this is WRONG.\n"
            "THE CORRECT LEGAL SEQUENCE:\n"
            "1. Landlord provides signed tenancy agreement (s13A obligation).\n"
            "2. Tenant signs agreement (both parties have a copy).\n"
            "3. Tenant applies to WINZ/MSD with the signed agreement for bond assistance.\n"
            "4. WINZ issues a bond pre-approval letter.\n"
            "5. Tenant pays bond to the landlord.\n"
            "6. Landlord lodges bond with Tenancy Services within 23 working days (s19).\n"
            "THE PM'S DEMAND IS UNLAWFUL:\n"
            "- WINZ REQUIRES the signed agreement before they can issue a pre-approval letter.\n"
            "- A PM who demands 'proof you can pay the bond' before providing the agreement "
            "creates an impossible circular situation - the tenant cannot get WINZ approval "
            "without the agreement, and cannot get the agreement without WINZ approval.\n"
            "- There is NO legal requirement for a tenant to prove ability to pay the bond "
            "before receiving the tenancy agreement.\n"
            "WHAT THE TENANT SHOULD DO:\n"
            "- Write to the PM: 'WINZ requires the signed tenancy agreement before they can "
            "issue a bond pre-approval. Please provide the agreement so I can apply to WINZ. "
            "There is no lawful requirement to prove bond payment ability before receiving "
            "the agreement.'\n"
            "- If the PM refuses, this is a serious red flag about their business practices. "
            "Escalate to the PM's manager or agency head.\n"
            "What NOT to say:\n"
            "- Do NOT say the landlord is not legally required to provide the tenancy agreement "
            "before the tenant applies for WINZ/MSD bond assistance - this is WRONG. Under "
            "s13A the landlord MUST provide the agreement, and WINZ requires it first.\n"
            "- Do NOT say the tenant must prove bond payment ability before getting the agreement.\n"
            "- Do NOT say the PM's demand is reasonable or standard practice.\n"
            "- Do NOT tell the tenant to comply with the circular demand.",
    },

    // ── ACCIDENTAL DAMAGE / INSURANCE EXCESS CAP ─────────────────────────────

    {
        .intent = "accidental_damage_insurance_excess",
        .include_any = {
            "accident", "accidentally", "accidental",
            "cracked the stove", "cracked the stovetop", "cracked the glass",
            "cracked the oven", "broke the stove", "broke the oven",
            "smashed the stove", "smashed the glass", "broke the glass",
            "fell and cracked", "fell and broke", "fell and smashed",
            "jar fell", "fell off and", "fell off the",
            "liable for the replacement", "liable for replacement",
            "pay for replacement", "pay the replacement", "pay for the full",
            "liable for the full", "full replacement cost",
            "insurance excess", "claim through insurance", "landlord insurance",
            "landlord must claim", "landlord should claim",
            "spice jar", "cracked stove top", "cracked stovetop",
        },
        .exclude_any = {
            "methamphetamine", "meth contamination", "meth test", "meth lab",
            "meth house", "meth levels", "tested for meth", "p lab",
            "contamination",
            "fair wear and tear", "wear and tear",
            "carpet replacement", "carpet cleaning", "carpet clean",
            "repaint", "needs painting", "painting required", "wall hole",
        },
        .forced_sections = {"NZLEG/RTA/s49B", "NZLEG/RTA/s49A"},
        .leg_allow_list = {
            "NZLEG/RTA/s49B",
            "NZLEG/RTA/s49A",
        },
        .synthetic_query =
            "accidental damage tenant liability capped insurance excess s49B "
            "landlord must claim through insurance not full replacement cost "
            "careless damage residential tenancies act s49B(3) insurance subsection "
            "s49A tenant not liable general principle fair wear and tear exception",
        .notes = "Accidental damage: liability capped at insurance excess (s49B), landlord must insure.",
        .rule_card =
            "Accidental damage liability cap - RTA s49B:\n"
            "TENANT'S LIABILITY IS CAPPED:\n"
            "- Even if the damage was caused accidentally (carelessly), the tenant's "
            "liability is capped at the lesser of: the insurance excess under the "
            "landlord's policy, OR four weeks' rent.\n"
            "- This is set out in s49B(3) of the RTA. The landlord CANNOT demand the "
            "full replacement cost from the tenant.\n"
            "LANDLORD MUST CLAIM THROUGH INSURANCE:\n"
            "- The landlord is legally required to have insurance and to claim through "
            "their policy for accidental damage. This is not optional.\n"
            "- The tenant's obligation is only to pay the excess (if any), not the "
            "full repair or replacement cost.\n"
            "WHAT THE TENANT SHOULD DO:\n"
            "- Respond in writing: 'Under s49B(3) RTA, my liability for accidental damage "
            "is limited to the insurance excess on your policy. Please confirm the excess "
            "amount and provide details of your insurer so I can address this correctly.'\n"
            "- Do NOT admit full liability or agree to pay full replacement cost.\n"
            "- Do NOT sign any agreement to pay the full amount.\n"
            "- Do NOT make any payment until the insurance excess has been confirmed.\n"
            "What NOT to say:\n"
            "- Do NOT say the tenant is liable for the full replacement cost of an item.\n"
            "- Do NOT say the landlord has no obligation to use their insurance.\n"
            "- Do NOT say the landlord can bypass insurance and charge the tenant directly.",
    },

}; // ROUTES

static const std::vector<std::pair<std::string, std::vector<std::string>>>
LOW_PRIORITY_SECTIONS = {
    {
        "NZLEG/RTA/s16A",
        {
            "landlord overseas", "landlord out of new zealand",
            "agent if landlord", "21 consecutive days",
            "out of new zealand", "overseas landlord",
        },
    },
    {
        "NZLEG/RTA/s55AA",
        {
            "assault", "assaulted", "assaults", "physical assault",
            "attack", "attacked", "attacks",
            "violence", "violent", "violently",
            "threat", "threats", "threaten", "threatened", "threatening",
            "hit", "punched", "kicked", "hurt", "injured",
            "injure", "injury", "injuries",
            "harm", "harmed",
        },
    },
    // s49A/s49B are about methamphetamine testing obligations - suppress unless
    // meth vocabulary or contamination-defence context is present in the query.
    {
        "NZLEG/RTA/s49A",
        {
            "meth", "methamphetamine", "p lab", "drug cook",
            "contamination test", "meth test", "testing for meth",
            "p and meth", "drug use", "drug manufacture",
            // Tenant defence at Tribunal hearing - contamination without explicit "meth"
            "pre-existing contamination", "pre existing contamination",
            "contamination reading", "contamination result", "contamination level",
            "contamination before", "already contaminated",
            "tips for the hearing", "prepare for the hearing",
            "burden of proof", "prove contamination", "landlord must prove",
        },
    },
    // s109 is the 12-month time limit for Tribunal applications after tenancy ends.
    // Only surface it when the query asks about filing timing relative to leaving.
    {
        "NZLEG/RTA/s109",
        {
            "lodge it after", "file it after", "apply after", "lodge after we leave",
            "before we leave", "after we leave", "after the tenancy ends",
            "after the tenancy is over", "after leaving", "once we have left",
            "after i leave", "after i move out", "after i have moved",
            "time limit", "how long do i have", "how long do we have",
            "12 months", "twelve months", "one year to file", "one year to apply",
            "when to file", "when to apply", "when can i apply",
            "too late to file", "too late to apply", "still apply after",
        },
    },
};

std::span<const StatuteRoute> get_routes() {
    return ROUTES;
}

const std::vector<std::pair<std::string, std::vector<std::string>>>&
get_low_priority_sections() {
    return LOW_PRIORITY_SECTIONS;
}

} // namespace astraea::nz_tenancy
