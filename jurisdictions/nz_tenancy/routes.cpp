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
            "crack", "cracked", "broken mirror",
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
        .forced_sections = {"NZLEG/RTA/s49A", "NZLEG/RTA/s49B", "NZLEG/RTA/s40"},
        .synthetic_query =
            "tenant not liable fair wear tear exception section 49A damage "
            "landlord cannot charge deterioration reasonable use natural forces "
            "residential tenancies act",
        .notes = "Tenant damage liability and fair wear and tear exception.",
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
            "install", "installed",
            "improvement",
        },
        .require_context_any = {
            "consent", "permission",
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
    },

    {
        .intent = "repairs_tenant_not_at_fault",
        .require_context_any = {
            "landlord", "property manager", "pm",
            "tenant", "rental", "tenancy",
            "heater", "heat pump", "appliance", "chattel",
            "stove", "oven", "hot water", "washing machine",
            "garage door", "lock", "fridge", "dishwasher",
        },
        .include_any = {
            "repair", "repairs", "fix", "fixed",
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
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s40"},
        .guidance_sources = {"MANUAL/damage-and-repairs"},
        .synthetic_query =
            "landlord repair obligation tenant not responsible fair wear and tear "
            "tenant did not cause damage heater appliance broken repair cost "
            "Residential Tenancies Act section 45 section 40",
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
            "leak", "leaking", "dripping",
            "weathertight", "habitable", "uninhabitable",
            "appliance", "oven", "stove", "fridge",
            "landlord obligation", "landlord's obligation",
            "s45",
            "pest", "pests", "pest control", "infestation", "infested",
            "spider", "spiders", "rat", "rats", "mice", "mouse",
            "cockroach", "cockroaches", "ant infestation", "fleas", "bedbugs",
            "bug", "bugs", "insect", "insects",
            "exterminator", "fumigation", "fumigated", "bitten",
            "wasp", "wasps", "wasp nest", "wasp nests", "bee", "bees", "beehive",
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
            "drainage", "drain blocked", "puddle at", "water pooling",
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
        .synthetic_query =
            "landlord responsibility maintain premises reasonable state repair "
            "section 45 habitable condition heating hot water weathertight "
            "residential tenancies act tenant remedies maintenance obligations "
            "equipment broke naturally not caused by tenant landlord must fix "
            "heater shower kitchen appliance stopped working tenant not liable",
        .notes = "Landlord maintenance and repair obligations (s45).",
    },

    // ── TENANCY AGREEMENT & PARTIES ───────────────────────────────────────────

    {
        .intent = "agreement_form",
        .include_any = {
            "tenancy agreement", "written agreement", "copy of agreement",
            "sign agreement", "signing agreement", "before signing",
            "provide agreement", "give the agreement", "before getting the agreement",
            "form of agreement", "written tenancy", "contents of agreement",
            "pet clause", "pets allowed", "no pets", "pets not allowed",
            "cats allowed", "dogs allowed", "cat allowed", "dog allowed",
            "allow pets", "allow cats", "allow dogs",
            "pet policy", "pet bond", "no pet",
            "new pet rules", "suitable for pets", "not suitable for pets",
            "property is not suitable for pets", "property suitable for pets",
            "fish tank", "aquarium", "fish tank permission",
            "change payment date", "change my payment date", "payment date",
            "rent payment date",
            "pet regulations", "pet regulation", "report the landlord", "report landlord",
            "renew our contract", "renew our lease", "renew rental contract",
            "shorter term", "12 month fixed term", "minimum fixed term",
            "new agency", "agency changed", "new property manager", "pm changed",
            "changed property manager", "change of agency", "new pm",
            "pet request", "pet application", "pet approval", "pet request form",
            "family pets", "new pet laws", "pets new law",
            "age of pet", "age of the cat", "age of the dog", "discriminate on age",
            "have a dog", "have a cat", "have a pet", "want a dog", "want a cat",
            "want a pet", "keep a dog", "keep a cat", "keep a pet",
            "get a dog", "get a cat", "get a pet", "push for a pet",
            "pet cover letter", "applying with a pet", "applying for a house with a pet",
            "pet reference", "pet resume",
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
            "not set up for pets", "set up for pets", "set up for a pet",
            "house trained", "house-trained", "neutered", "desexed",
            "my cats", "my dogs", "my pets", "have 2 cats", "have 2 dogs",
            "have two cats", "have two dogs", "have three cats", "have three dogs",
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
            "has cats", "has a cat", "tenant with cats",
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
        },
        .forced_sections = {"NZLEG/RTA/s13A", "NZLEG/RTA/s13B"},
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
        .guidance_sources = {
            "MANUAL/how-to-apply-for-a-bond-refund",
            "MANUAL/bonds",
        },
        .synthetic_query =
            "general bond landlord maximum bond amount four weeks rent section 18 19 "
            "residential tenancies act bond lodgment duties receipt chief executive",
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
            "bond for my animal", "bond for the animal",
            "s18aa", "s18AA",
        },
        .forced_sections = {"NZLEG/RTA/s18", "NZLEG/RTA/s18A", "NZLEG/RTA/s18AA", "NZLEG/RTA/s19", "NZLEG/RTA/s22"},
        .synthetic_query =
            "pet bond cat dog animal s18AA 1 December 2025 retroactive "
            "landlord charge pet bond section 18AA residential tenancies act",
        .notes = "Pet bond rules (s18AA, 1 Dec 2025 regime) - separate from general bond.",
        .rule_card =
            "Pet bond rules (RTA s18AA, s18A, s22):\n"
            "Pet bond retroactivity rule (CRITICAL):\n"
            "- The pet bond regime (s18AA) took effect 1 December 2025. A landlord may only charge "
            "a pet bond for a pet that was agreed to IN WRITING on or after 1 December 2025.\n"
            "- If the tenant's pet was approved BEFORE 1 December 2025, the landlord CANNOT charge "
            "a pet bond retroactively. Demanding one violates s18A (landlord must not require "
            "security other than permitted bond).\n"
            "- Safe answer: 'Because your cat was approved before 1 December 2025, the new pet "
            "bond rules do not apply to that pet and no pet bond can be charged.'\n"
            "Mid-tenancy bond deduction rule:\n"
            "- A landlord CANNOT deduct from the bond or claim insurance excess during a tenancy. "
            "Bond payments are only made at end-of-tenancy via the s22 application process.\n"
            "- Never say a landlord can recover costs from the bond while the tenancy is ongoing.",
    },

    {
        .intent = "landlord_entry",
        .include_any = {
            "landlord entry", "landlord enter", "right of entry",
            "inspection notice", "24 hour notice", "24 hours notice",
            "inspection report", "routine inspection",
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
            "take photos", "photos at inspection", "photograph my", "photos of my",
            "inspection photos", "inspection photo", "photos from inspection",
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
        .forced_sections = {"NZLEG/RTA/s48"},
        .synthetic_query =
            "landlord right of entry inspection notice 24 hours section 48 "
            "residential tenancies act access premises",
        .notes = "Landlord entry and inspection rules (s48).",
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
            "sublet", "subletting", "sublease", "sub-letting", "sub-lease",
            "renting from a flatmate", "paying my flatmate", "flatmate charges",
            "flatmate is my landlord", "room from a flatmate",
            "he pays the landlord", "she pays the landlord",
            "paying through my flatmate", "pays the landlord for me",
        },
        .forced_sections = {"NZLEG/RTA/s5"},
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
            "evict", "eviction",
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
        .forced_sections = {"NZLEG/RTA/s51", "NZLEG/RTA/s60A"},
        .guidance_sources = {
            "MANUAL/giving-notice-to-end-a-tenancy",
            "MANUAL/ending-a-tenancy",
        },
        .synthetic_query =
            "landlord terminate periodic tenancy notice 90 days 42 days "
            "section 51 60A residential tenancies act tenant notice 21 days "
            "lawful grounds termination",
        .notes = "Termination of periodic tenancy, notice periods (s51).",
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
        .synthetic_query =
            "healthy homes standards heating insulation ventilation moisture draught "
            "residential tenancies act section 138B landlord obligations "
            "extractor fan ceiling underfloor insulation draught stopping ground moisture barrier",
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
            "the landlord is in breach of the Healthy Homes heating standard.",
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
        .synthetic_query =
            "landlord obligations lighting smoke alarm carport laundry "
            "healthy homes standards ventilation extraction fan requirements "
            "habitable space facilities residential tenancy",
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
        .forced_sections = {"NZLEG/RTA/s28", "NZLEG/RTA/s28A"},
        .guidance_sources = {"MANUAL/rent-increases-and-reductions"},
        .synthetic_query =
            "notice to increase rent landlord section 28 28A residential tenancies act "
            "rent increase order unforeseen expenses 90 days",
        .notes = "Rent increases by notice or order (s28, s28A).",
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
            "s55b", "s55c",
        },
        .forced_sections = {"NZLEG/RTA/s55B", "NZLEG/RTA/s55C"},
        .synthetic_query =
            "tenant family violence domestic violence protection order "
            "terminate tenancy early section 55B 55C residential tenancies act "
            "victim safety notice without consent co-tenant",
        .notes = "Family violence exit - tenant can terminate without notice using s55B/s55C.",
    },

    // ── TENANT RIGHTS & DISPUTES ──────────────────────────────────────────────

    {
        .intent = "quiet_enjoyment",
        .include_any = {
            "quiet enjoyment", "peaceful enjoyment", "peaceful possession",
            "s38",
            "harass", "harassment", "landlord harassing",
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
        .synthetic_query =
            "landlord obligation quiet enjoyment tenant peaceful possession "
            "section 38 residential tenancies act interference harassment "
            "noisy disruptive neighbours landlord must not interfere",
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
        },
        .forced_sections = {"NZLEG/RTA/s136", "NZLEG/RTA/s13C"},
        .synthetic_query =
            "electronic communication notice text message email valid notice RTA "
            "section 136 service of documents electronic address written notice "
            "residential tenancies act s13C in writing",
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
            "Retrospective landlord challenge - ANSWER TEMPLATE:\n"
            "- If the TENANCY HAS ALREADY ENDED and the landlord is now asking 'did you give "
            "21 days notice?', answer the question directly: state whether the ORIGINAL notice "
            "appears valid based on the facts.\n"
            "- If the facts show the notice was sent electronically, received by the landlord, "
            "gave more than the minimum required days, and the landlord did not immediately "
            "dispute it at the time, STATE CLEARLY that the notice appears to have been valid "
            "under s13C. Cite s13C in the answer.\n"
            "- Do NOT tell the tenant to give more notice now - the tenancy is over.\n"
            "- Do NOT say the tenant needs to do anything further regarding notice after the "
            "tenancy has ended.\n"
            "- A landlord who arranged a final inspection, acknowledged the move-out date, or "
            "accepted keys without immediately disputing the notice cannot easily claim later "
            "that the notice was invalid.\n"
            "- If the notice gave well over 21 days (e.g., 60 days, 90 days, 105 days), say so "
            "clearly: 'You gave [X] days notice which is more than the 21 days required.'\n"
            "TENANT vs LANDLORD notice period (CRITICAL):\n"
            "- Tenants ending a PERIODIC tenancy need a MINIMUM of 21 days notice (s51(2A)).\n"
            "- The 90-days rule in s51(1) and 42-days rule in s51(2) apply to LANDLORDS only.\n"
            "- A tenant who gives MORE than 21 days notice has fully complied. Extra advance "
            "notice does not invalidate the notice or require a separate final notice.\n"
            "- Do NOT say a tenant failed to give proper notice if the notice was given more "
            "than 21 days before the end date.",
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
        },
        .forced_sections = {"NZLEG/RTA/s50", "NZLEG/RTA/s60A", "NZLEG/RTA/s61"},
        .synthetic_query =
            "fixed term tenancy mutual agreement terminate early section 50 "
            "agreement to end tenancy s60A fixed term expiry liability "
            "section 61 abandonment mitigation residential tenancies act",
        .notes = "P1 guard: fixed-term mutual agreement (s50) displaces s60A default end-date liability.",
        .rule_card =
            "Fixed-term mutual agreement guard (RTA s50 vs s60A vs s61):\n"
            "When the question shows BOTH PARTIES agreed on an earlier end date under s50:\n"
            "- That agreed date IS the binding end date. The tenancy ENDED on that date.\n"
            "- The tenant owes NO rent beyond the agreed end date.\n"
            "- The landlord CANNOT retroactively demand additional notice or extend the tenancy "
            "once both parties agreed on an end date and the tenant vacated on that date.\n"
            "- Do NOT apply s61 liability 'until original fixed-term end date' when s50 mutual "
            "agreement established an earlier binding end date.\n"
            "- Do NOT say the tenant is still liable for rent after the agreed-upon end date.\n"
            "Priority rule: s50 mutual agreement OVERRIDES s60A fixed-term expiry default. "
            "Analyse s50 FIRST. Only apply s60A/s61 if there was NO mutual agreement to end early.\n"
            "Fixed-term vs periodic distinction: A fixed-term tenancy ends on its agreed date "
            "with NO notice required - a landlord who applies periodic-tenancy notice rules to "
            "a fixed-term that was mutually ended early is misapplying the law.",
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
            "- Filing does not automatically stay the Tribunal's order (s117(10)).",
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
        .synthetic_query =
            "healthy homes standards bedroom room count heating requirement "
            "qualifying heater main living room bedroom classification "
            "HHS regulations residential tenancy room",
        .notes = "P1 guard: HHS bedroom/room classification - advertising alone is not conclusive.",
        .rule_card =
            "Healthy Homes room classification guard:\n"
            "When a question is about how many bedrooms count for HHS compliance:\n"
            "- Do NOT rely solely on advertising wording to determine whether a room is a bedroom. "
            "Advertising is evidence but not conclusive.\n"
            "- Consider the actual room use, size, layout, and how the space was rented and used.\n"
            "- Safe answer pattern: 'The advertisement is relevant but not always conclusive. "
            "The actual room and how it was used under the tenancy may matter for HHS compliance.'",
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
            "sublet", "subletting", "subletted", "subletter", "sub-let", "sub let",
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
        .exclude_any = {"bond", "bond refund", "tribunal results", "appeal"},
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s56"},
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
        .synthetic_query =
            "lease break fee fixed term tenancy section 44A early termination "
            "reletting costs actual costs itemised invoice residential tenancies act",
        .notes = "Fixed-term break fees (s44A) - actual costs only, no generic admin fees.",
        .rule_card =
            "Lease break fees (RTA s44A):\n"
            "When a tenant breaks a fixed-term tenancy early, the landlord may only claim:\n"
            "- ACTUAL AND REASONABLE costs of reletting (advertising, showing property, etc.)\n"
            "- Any difference in rent if the new tenant pays less (mitigated by s38 duty)\n"
            "NOT allowed: preset packages, admin fees, 'tenancy finalisation' fees, processing "
            "fees, or any fee not tied to actual out-of-pocket reletting costs.\n"
            "- An invoice with no itemisation is invalid - the tenant can refuse to pay and "
            "demand a full breakdown with receipts before paying anything.\n"
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
        .synthetic_query =
            "Tenancy Tribunal order mediation settlement binding enforcement "
            "breach of order s78 orders exemplary damages s95A compliance "
            "residential tenancies act landlord non-compliance",
        .notes = "Existing Tribunal/mediation order being ignored or breached - s78, s95A.",
        .rule_card =
            "Existing Tribunal order or mediation agreement (RTA s78, s95A, s38):\n"
            "When the user describes an existing Tribunal order, sealed order, consent order, "
            "or Tribunal-mediated settlement agreement being ignored:\n"
            "- Do NOT treat this as a new tenancy dispute starting from scratch. The order "
            "IS legally binding and cannot be overridden by the other party.\n"
            "- The lawful terms in the order (e.g., a payment date, bond split, repair "
            "obligation) take precedence over any subsequent informal demands.\n"
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
            "tribunal results", "appeal",
            // Exclude tenant-caused damage (tenant broke it, not landlord failing to fix)
            "i broke", "i accidentally", "i cracked", "my fault", "i damaged",
            "accident where i", "fell off and cracked", "i spilled", "i knocked",
        },
        .forced_sections = {"NZLEG/RTA/s45", "NZLEG/RTA/s56", "NZLEG/RTA/s77", "NZLEG/RTA/s78"},
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
        },
        .exclude_any = {
            "inspection report", "bond lodged", "lodge bond", "bond not lodged",
            "tribunal order bond", "bond after tribunal",
        },
        .forced_sections = {"NZLEG/RTA/s22", "NZLEG/RTA/s40"},
        .synthetic_query =
            "exit inspection bond refund process move out final inspection "
            "section 22 bond claim evidence photos tenant obligations "
            "residential tenancies act bond return",
        .notes = "Exit inspection timing and bond refund process (s22, s40).",
        .rule_card =
            "Exit inspection and bond refund process (RTA s22, s40):\n"
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
            "What NOT to say:\n"
            "- Do NOT confuse the exit inspection for bond purposes with routine landlord "
            "entry for viewings or routine inspections - these are different processes.\n"
            "- Do NOT say the PM is legally required to complete the exit inspection same "
            "day or within 24 hours - no such rule exists.\n"
            "- Do NOT say the tenant cannot receive their bond until the exit inspection "
            "is done - if the PM is unreasonably delaying, the tenant can apply to the "
            "Tribunal for bond release.",
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
        .exclude_any = {"pet bond", "pet bonds", "s18aa", "s18AA"},
        .forced_sections = {"NZLEG/RTA/s42E", "NZLEG/RTA/s42F"},
        .synthetic_query =
            "tenant keep pet dog cat landlord consent refuse request written "
            "section 42E 42F pet permission 21 days automatic consent "
            "residential tenancies act new pet rules",
        .notes = "Pet permission request process (s42E/s42F) - 21-day rule, silence = consent.",
        .rule_card =
            "Pet permission process (RTA s42E, s42F):\n"
            "How a tenant requests permission to keep a pet:\n"
            "- Submit a WRITTEN REQUEST to the landlord specifying: the type of pet, "
            "breed, age, size, and how the tenant will care for it and prevent damage.\n"
            "- The landlord has 21 DAYS to respond in writing. If the landlord does NOT "
            "respond within 21 days, consent is AUTOMATICALLY GRANTED (silence = consent).\n"
            "Grounds for refusal:\n"
            "- The landlord may refuse only on reasonable grounds (e.g., the property type "
            "makes the pet unsuitable, body corporate rules prohibit pets, the pet poses a "
            "genuine risk to the property or other residents).\n"
            "- A refusal must be in writing and give the specific reason.\n"
            "- The tenant can challenge an unreasonable refusal at the Tribunal.\n"
            "What NOT to say:\n"
            "- Do NOT say the tenant has an absolute right to keep any pet regardless of "
            "property type or body corporate rules - the landlord CAN refuse on reasonable grounds.\n"
            "- Do NOT say the tenant has no rights - the 21-day silence rule is a significant "
            "protection and landlords must give reasons for any refusal.",
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
            "s35",
        },
        .forced_sections = {"NZLEG/RTA/s35"},
        .synthetic_query =
            "tenant right inspection report property inspection photographs "
            "section 35 residential tenancies act copy inspection report",
        .notes = "Tenant right to receive inspection reports and photos (s35).",
        .rule_card =
            "Tenant right to inspection reports (RTA s35):\n"
            "- Tenants have the right to request and receive copies of ANY inspection "
            "report prepared in relation to the premises, including routine inspection "
            "reports and any photographs taken during those inspections.\n"
            "- A property manager CANNOT refuse to provide the inspection report or "
            "demand that the tenant specify which parts they want and why - the tenant "
            "is entitled to the full report and photos on request.\n"
            "- The request should be made in writing to create a record.\n"
            "- If the property manager continues to refuse, the tenant can apply to the "
            "Tenancy Tribunal for an order requiring disclosure.\n"
            "- Do NOT say the tenant is not entitled to the inspection report or must "
            "justify the request.",
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
            "assault", "physical assault", "attacked", "attack",
            "violence", "violent", "threatened", "threat",
            "hit", "punched", "kicked", "hurt", "injured", "harm",
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
