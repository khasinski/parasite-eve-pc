/* Parasite Eve's own calls: a pack, registered through the "phonecalls" service like any other.
 *
 * Mostly a random pool, by tone and story day (the save screen's Day); the sewer's emergency phone
 * has a set call (priority, once) and one of its own in the pool there. Spoiler-safe: nothing
 * past the call's day, no Eve by name, nothing on mitochondria, and the people the game treats
 * seriously (Aya's mother, Maya, Melissa, Ben) stay out of every call. Aya is "{name}", the name
 * the player typed. Lines are wrapped to the boxes when they play; "Check calls" (F1 > Phone
 * Calls > Debug) measures them with the game's font.
 *
 * Keys are saved in players' saves: never rename one. */
#include <stddef.h>

#include "pe_phone.h"

#define A(text) {PE_CALL_AYA, text}
#define C(text) {PE_CALL_CALLER, text}
#define LINES(name) name, (int)(sizeof name / sizeof name[0])

/* ---- Day 1: Carnegie Hall, Christmas Eve ---- */

static const PePhoneLine k_nypd_desk[] = {
    C("17th Precinct, Sergeant Hall speaking."),
    A("It's Brea. I'm at Carnegie Hall. Send everyone you've got."),
    C("Brea, it's Christmas Eve. Everyone I've got is one guy."),
    A("Then send him."),
    C("He's eating."),
};
static const PePhoneLine k_daniel_cordon[] = {
    C("Aya? We've got the whole block taped off out here."),
    A("Keep everyone out. Whatever this is, it isn't over."),
    C("Then get out of there."),
    A("Not yet."),
};
static const PePhoneLine k_pizza[] = {
    C("Tony's Pizza, how can I help?"),
    A("Can you deliver to Carnegie Hall?"),
    C("Sure thing. Any special instructions?"),
    A("Don't come inside. Ever."),
};
static const PePhoneLine k_wrong_number[] = {
    C("Hello? Is this the opera?"),
    A("...Sort of."),
    C("Is the show any good?"),
    A("The ending was a real scorcher."),
};

/* ---- Day 1: the sewer's emergency phone (M032) ---- */

static const short k_sewer[] = {32};
static const PePhoneLine k_sewer_emergency[] = {
    C("Emergency line. What's the situation?"),
    A("Officer Brea, NYPD. I'm in the sewers under Carnegie Hall."),
    C("...The sewers? Ma'am, how did you get down there?"),
    A("I followed someone. Send a unit to the gate by the Hall."),
    C("Every unit is tied up at the Hall. Hold your position."),
    A("That's not really an option."),
};
static const PePhoneLine k_sewer_static[] = {
    C("...Hello? You're breaking up."),
    A("I'm underground. Can you hear me?"),
    C("...kssh... repeat... kssh..."),
    A("Never mind."),
};

/* ---- any day ---- */

static const PePhoneLine k_daniel_checkin[] = {
    C("Aya? Where are you?"),
    A("Still inside. I'm fine."),
    C("You don't sound fine."),
    A("I'll call you when I'm out."),
};
static const PePhoneLine k_precinct_quiet[] = {
    C("17th Precinct."),
    A("Brea. Just checking in."),
    C("Copy that, Brea. Stay safe out there."),
};
static const PePhoneLine k_time_line[] = {
    C("At the tone, the time will be... late."),
    C("Temperature: rising."),
    A("Thanks. I noticed."),
};
static const PePhoneLine k_landlord[] = {
    C("Miss Brea, your rent is late."),
    A("I'm a little busy saving New York."),
    C("New York doesn't pay my rent."),
};
static const PePhoneLine k_salesman[] = {
    C("Ma'am, have you given any thought to fire insurance?"),
    A("More than you would believe."),
    C("Wonderful! Can I put you down for-"),
    A("No."),
};

/* ---- Day 1 and 2: Christmas ---- */

static const PePhoneLine k_radio[] = {
    C("Congratulations, you're caller nine!"),
    A("I'm what?"),
    C("Name that Christmas song and win a turkey!"),
    A("I'm a little busy right now."),
    C("Ooh, sorry, that's not it. Merry Christmas!"),
};

/* ---- Day 2 on: the precinct, Wayne, the Chief ---- */

static const PePhoneLine k_wayne_stock[] = {
    C("Precinct armory. Wayne here."),
    A("It's Aya. Anything new?"),
    C("Bring me junk and I'll make you something nice."),
};
static const PePhoneLine k_wayne_junk[] = {
    C("Got any more junk for me?"),
    A("A bag full. Most of it used to be guns."),
    C("Perfect. Bring it on by."),
};
static const PePhoneLine k_dispatch[] = {
    C("Dispatch. Go ahead."),
    A("Brea, checking in. Anything new?"),
    C("Calls from all over Manhattan. Animals acting strange."),
    A("Strange how?"),
    C("The kind nobody wants to write up."),
};
static const PePhoneLine k_baker_status[] = {
    C("Brea. Tell me you have good news."),
    A("I'm still standing, sir."),
    C("In this city right now, that counts."),
};
static const PePhoneLine k_baker_press[] = {
    C("The press is camped outside my office, Brea."),
    A("Tell them nothing, sir."),
    C("That's all I've been able to tell them."),
};
static const PePhoneLine k_coroner[] = {
    C("Medical Examiner's office."),
    A("Did the Carnegie Hall reports come in?"),
    C("They did. I've never seen anything like them."),
    A("Neither had I. Until last night."),
};
static const PePhoneLine k_hold[] = {
    C("NYPD switchboard. Please hold."),
    A("...Hello?"),
    C("Your call is important to us."),
    A("I'll call back."),
};
static const PePhoneLine k_library[] = {
    C("Public Library, reference desk."),
    A("Do you have anything on people bursting into flames?"),
    C("...Fiction or nonfiction?"),
    A("I was hoping you'd tell me."),
};

/* ---- Day 3 on ---- */

static const PePhoneLine k_maeda_samples[] = {
    C("Ah, Aya! I was hoping you would call."),
    A("Dr. Maeda. Any news on the samples?"),
    C("The cells are remarkable. If you find more, bring them to me."),
    A("I'm not carrying anything home for you."),
    C("Not even a small one?"),
};
static const PePhoneLine k_daniel_coffee[] = {
    C("You eat anything today?"),
    A("Does coffee count?"),
    C("Not three days running, it doesn't."),
};

/* ---- Day 4 on ---- */

static const PePhoneLine k_daniel_alone[] = {
    C("Aya. Whatever happens next, don't do it alone."),
    A("I won't."),
    C("I mean it."),
    A("So do I."),
};

/*                       key                  caller              tone             lines                      w  once day  */
static const PePhoneCall k_calls[] = {
    {"pe1/nypd_desk",      "NYPD Desk",        PE_CALL_FUNNY,   LINES(k_nypd_desk),        2, 1, 1, 1, 0, 0, NULL, 0, 0, NULL},
    {"pe1/daniel_cordon",  "Daniel",           PE_CALL_SERIOUS, LINES(k_daniel_cordon),    2, 1, 1, 1, 0, 0, NULL, 0, 0, NULL},
    {"pe1/pizza",          "Tony's Pizza",     PE_CALL_FUNNY,   LINES(k_pizza),            1, 0, 1, 1, 0, 0, NULL, 0, 0, NULL},
    {"pe1/wrong_number",   "???",              PE_CALL_FUNNY,   LINES(k_wrong_number),     1, 0, 1, 1, 0, 0, NULL, 0, 0, NULL},
    {"pe1/sewer_emergency","Emergency Line",   PE_CALL_SERIOUS, LINES(k_sewer_emergency),  1, 1, 1, 1, 0, 0, k_sewer, 1, 10, NULL},
    {"pe1/sewer_static",   "Emergency Line",   PE_CALL_FUNNY,   LINES(k_sewer_static),     2, 0, 1, 1, 0, 0, k_sewer, 1, 0, NULL},
    {"pe1/daniel_checkin", "Daniel",          PE_CALL_SERIOUS, LINES(k_daniel_checkin),   1, 0, 1, 5, 0, 0, NULL, 0, 0, NULL},
    {"pe1/precinct_quiet", "Desk Sergeant",    PE_CALL_SERIOUS, LINES(k_precinct_quiet),   1, 0, 0, 0, 0, 0, NULL, 0, 0, NULL},
    {"pe1/time_line",      "Time Line",        PE_CALL_FUNNY,   LINES(k_time_line),        1, 0, 0, 0, 0, 0, NULL, 0, 0, NULL},
    {"pe1/landlord",       "Landlord",         PE_CALL_FUNNY,   LINES(k_landlord),         1, 0, 0, 0, 0, 0, NULL, 0, 0, NULL},
    {"pe1/salesman",       "Salesman",         PE_CALL_FUNNY,   LINES(k_salesman),         1, 0, 0, 0, 0, 0, NULL, 0, 0, NULL},
    {"pe1/radio",          "Radio DJ",         PE_CALL_FUNNY,   LINES(k_radio),            1, 0, 1, 2, 0, 0, NULL, 0, 0, NULL},
    {"pe1/wayne_stock",    "Wayne",            PE_CALL_SERIOUS, LINES(k_wayne_stock),      1, 0, 2, 6, 0, 0, NULL, 0, 0, NULL},
    {"pe1/wayne_junk",     "Wayne",            PE_CALL_SERIOUS, LINES(k_wayne_junk),       1, 0, 2, 6, 0, 0, NULL, 0, 0, NULL},
    {"pe1/dispatch",       "Dispatch",         PE_CALL_SERIOUS, LINES(k_dispatch),         1, 0, 2, 4, 0, 0, NULL, 0, 0, NULL},
    {"pe1/baker_status",   "Chief Baker",      PE_CALL_SERIOUS, LINES(k_baker_status),     1, 0, 2, 5, 0, 0, NULL, 0, 0, NULL},
    {"pe1/baker_press",    "Chief Baker",      PE_CALL_SERIOUS, LINES(k_baker_press),      2, 1, 2, 2, 0, 0, NULL, 0, 0, NULL},
    {"pe1/coroner",        "Medical Examiner", PE_CALL_SERIOUS, LINES(k_coroner),          2, 1, 2, 2, 0, 0, NULL, 0, 0, NULL},
    {"pe1/hold",           "Switchboard",      PE_CALL_FUNNY,   LINES(k_hold),             1, 0, 2, 5, 0, 0, NULL, 0, 0, NULL},
    {"pe1/library",        "Library",          PE_CALL_FUNNY,   LINES(k_library),          1, 0, 2, 5, 0, 0, NULL, 0, 0, NULL},
    {"pe1/maeda_samples",  "Maeda",            PE_CALL_SERIOUS, LINES(k_maeda_samples),    1, 0, 3, 5, 0, 0, NULL, 0, 0, NULL},
    {"pe1/daniel_coffee",  "Daniel",           PE_CALL_SERIOUS, LINES(k_daniel_coffee),    1, 0, 3, 5, 0, 0, NULL, 0, 0, NULL},
    {"pe1/daniel_alone",   "Daniel",           PE_CALL_SERIOUS, LINES(k_daniel_alone),     2, 1, 4, 5, 0, 0, NULL, 0, 0, NULL},
};

void pe_calls_pe1_register(const PePhoneCallsApi *api) {
    size_t i;
    for (i = 0; i < sizeof k_calls / sizeof k_calls[0]; ++i) (void)api->add(&k_calls[i]);
}
