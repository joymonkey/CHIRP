#include "klatt_synth.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Phone Symbol Lookup & Helpers
// ============================================================================
struct PhoneSym {
    const char* name;
    KlattPhoneId id;
};

static const PhoneSym kPhoneSyms[] = {
    {"AA", PH_AA}, {"AE", PH_AE}, {"AH", PH_AH}, {"AO", PH_AO},
    {"AW", PH_AW}, {"AX", PH_AX}, {"AY", PH_AY}, {"B",  PH_B},
    {"CH", PH_CH}, {"D",  PH_D},  {"DH", PH_DH}, {"EH", PH_EH},
    {"ER", PH_ER}, {"EY", PH_EY}, {"F",  PH_F},  {"G",  PH_G},
    {"HH", PH_HH}, {"IH", PH_IH}, {"IY", PH_IY}, {"JH", PH_JH},
    {"K",  PH_K},  {"L",  PH_L},  {"M",  PH_M},  {"N",  PH_N},
    {"NG", PH_NG}, {"OW", PH_OW}, {"OY", PH_OY}, {"P",  PH_P},
    {"R",  PH_R},  {"S",  PH_S},  {"SH", PH_SH}, {"T",  PH_T},
    {"TH", PH_TH}, {"UH", PH_UH}, {"UW", PH_UW}, {"V",  PH_V},
    {"W",  PH_W},  {"Y",  PH_Y},  {"Z",  PH_Z},  {"ZH", PH_ZH},
};

static bool isKlattVowel(uint8_t id) {
    return (id >= PH_AA && id <= PH_UW);
}

static uint8_t parsePhoneToken(const char* tok, int len, uint8_t* outStress) {
    *outStress = 0;
    if (len <= 0) return PH_COUNT;
    char clean[4] = {0, 0, 0, 0};
    int ci = 0;
    for (int i = 0; i < len && ci < 3; i++) {
        char c = tok[i];
        if (c == '1' || c == '2') {
            *outStress = 1;
        } else if (c == '0') {
            *outStress = 0;
        } else if (isalpha((unsigned char)c)) {
            clean[ci++] = (char)toupper((unsigned char)c);
        }
    }
    if (ci == 0) return PH_COUNT;
    for (size_t i = 0; i < sizeof(kPhoneSyms) / sizeof(kPhoneSyms[0]); i++) {
        if (strcmp(kPhoneSyms[i].name, clean) == 0) {
            return (uint8_t)kPhoneSyms[i].id;
        }
    }
    return PH_COUNT;
}

static void appendPhoneString(const char* phStr, bool isWordStart,
                              KlattPhoneme* out, int& outCount, int maxPhones) {
    if (!phStr) return;
    const char* p = phStr;
    bool first = isWordStart;
    while (*p && outCount < maxPhones) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ' ') p++;
        uint8_t stress = 0;
        uint8_t id = parsePhoneToken(start, (int)(p - start), &stress);
        if (id < PH_COUNT) {
            out[outCount].id = id;
            out[outCount].stress = isKlattVowel(id) ? stress : 0;
            out[outCount].flags = first ? KLATT_FL_WORD_START : KLATT_FL_NONE;
            outCount++;
            first = false;
        }
    }
}

// ============================================================================
// Compact Exception & CHIRP Domain Dictionary (Flash)
// ============================================================================
struct DictEntry {
    const char* word;
    const char* phones;
};

static const DictEntry kExceptionDict[] = {
    // Numbers
    {"0", "Z IY1 R OW"}, {"1", "W AH1 N"}, {"2", "T UW1"}, {"3", "TH R IY1"},
    {"4", "F AO1 R"}, {"5", "F AY1 V"}, {"6", "S IH1 K S"}, {"7", "S EH1 V AX N"},
    {"8", "EY1 T"}, {"9", "N AY1 N"},
    {"ZERO", "Z IY1 R OW"}, {"ONE", "W AH1 N"}, {"TWO", "T UW1"}, {"THREE", "TH R IY1"},
    {"FOUR", "F AO1 R"}, {"FIVE", "F AY1 V"}, {"SIX", "S IH1 K S"}, {"SEVEN", "S EH1 V AX N"},
    {"EIGHT", "EY1 T"}, {"NINE", "N AY1 N"}, {"TEN", "T EH1 N"}, {"ELEVEN", "IH L EH1 V AX N"},
    {"TWELVE", "T W EH1 L V"}, {"THIRTEEN", "TH ER T IY1 N"}, {"FOURTEEN", "F AO R T IY1 N"},
    {"FIFTEEN", "F IH F T IY1 N"}, {"SIXTEEN", "S IH K S T IY1 N"}, {"SEVENTEEN", "S EH V AX N T IY1 N"},
    {"EIGHTEEN", "EY T IY1 N"}, {"NINETEEN", "N AY N T IY1 N"}, {"TWENTY", "T W EH1 N T IY"},
    {"THIRTY", "TH ER1 T IY"}, {"FORTY", "F AO1 R T IY"}, {"FIFTY", "F IH1 F T IY"},
    {"SIXTY", "S IH1 K S T IY"}, {"SEVENTY", "S EH1 V AX N T IY"}, {"EIGHTY", "EY1 T IY"},
    {"NINETY", "N AY1 N T IY"}, {"HUNDRED", "HH AH1 N D R AX D"}, {"THOUSAND", "TH AW1 Z AX N D"},
    {"MILLION", "M IH1 L Y AX N"}, {"BILLION", "B IH1 L Y AX N"}, {"MINUS", "M AY1 N AX S"},
    {"PLUS", "P L AH1 S"}, {"PERCENT", "P ER S EH1 N T"},
    {"ZEROTH", "Z IY1 R OW TH"}, {"SECOND", "S EH1 K AX N D"}, {"THIRD", "TH ER1 D"},
    {"FOURTH", "F AO1 R TH"}, {"FIFTH", "F IH1 F TH"}, {"SIXTH", "S IH1 K S TH"},
    {"SEVENTH", "S EH1 V AX N TH"}, {"EIGHTH", "EY1 T TH"}, {"NINTH", "N AY1 N TH"},
    {"TENTH", "T EH1 N TH"}, {"ELEVENTH", "IH L EH1 V AX N TH"}, {"TWELFTH", "T W EH1 L F TH"},
    {"THIRTEENTH", "TH ER T IY1 N TH"}, {"FOURTEENTH", "F AO R T IY1 N TH"},
    {"FIFTEENTH", "F IH F T IY1 N TH"}, {"SIXTEENTH", "S IH K S T IY1 N TH"},
    {"SEVENTEENTH", "S EH V AX N T IY1 N TH"}, {"EIGHTEENTH", "EY T IY1 N TH"},
    {"NINETEENTH", "N AY N T IY1 N TH"}, {"TWENTIETH", "T W EH1 N T IY AX TH"},
    {"THIRTIETH", "TH ER1 T IY AX TH"}, {"FORTIETH", "F AO1 R T IY AX TH"},
    {"FIFTIETH", "F IH1 F T IY AX TH"}, {"SIXTIETH", "S IH1 K S T IY AX TH"},
    {"SEVENTIETH", "S EH1 V AX N T IY AX TH"}, {"EIGHTIETH", "EY1 T IY AX TH"},
    {"NINETIETH", "N AY1 N T IY AX TH"}, {"HUNDREDTH", "HH AH1 N D R AX D TH"},
    {"THOUSANDTH", "TH AW1 Z AX N D TH"}, {"MILLIONTH", "M IH1 L Y AX N TH"},
    {"BILLIONTH", "B IH1 L Y AX N TH"},
    {"OCLOCK", "AX K L AA1 K"}, {"O'CLOCK", "AX K L AA1 K"},
    {"VOLT", "V OW1 L T"}, {"VOLTS", "V OW1 L T S"},
    {"MILLIVOLT", "M IH1 L IH V OW L T"}, {"MILLIVOLTS", "M IH1 L IH V OW L T S"},
    {"AMP", "AE1 M P"}, {"AMPS", "AE1 M P S"},
    {"MILLIAMP", "M IH1 L IH AE M P"}, {"MILLIAMPS", "M IH1 L IH AE M P S"},
    {"HERTZ", "HH ER1 T S"}, {"KILOHERTZ", "K IH1 L AX HH ER T S"},
    {"MEGAHERTZ", "M EH1 G AX HH ER T S"}, {"GIGAHERTZ", "G IH1 G AX HH ER T S"},
    {"MILLISECOND", "M IH1 L IH S EH K AX N D"}, {"MILLISECONDS", "M IH1 L IH S EH K AX N D Z"},
    {"SECONDS", "S EH1 K AX N D Z"}, {"DECIBEL", "D EH1 S AX B EH L"},
    {"DECIBELS", "D EH1 S AX B EH L Z"}, {"KILOBYTE", "K IH1 L AX B AY T"},
    {"KILOBYTES", "K IH1 L AX B AY T S"}, {"MEGABYTE", "M EH1 G AX B AY T"},
    {"MEGABYTES", "M EH1 G AX B AY T S"}, {"GIGABYTE", "G IH1 G AX B AY T"},
    {"GIGABYTES", "G IH1 G AX B AY T S"}, {"DOLLAR", "D AA1 L ER"},
    {"DOLLARS", "D AA1 L ER Z"}, {"DEGREE", "D IH G R IY1"},
    {"DEGREES", "D IH G R IY1 Z"}, {"VOLTAGE", "V OW1 L T IH JH"},
    {"TEMPERATURE", "T EH1 M P R AX CH ER"}, {"WAVE", "W EY1 V"},
    {"DOUBLE", "D AH1 B AX L"}, {"AITCH", "EY1 CH"},
    {"BITS", "B IH1 T S"}, {"PER", "P ER1"},

    // CHIRP System Feedback, Sci-Fi & Droid Vocabulary
    {"BAUD", "B AO1 D"}, {"RATE", "R EY1 T"}, {"SETTING", "S EH1 T IH NG"},
    {"BANK", "B AE1 NG K"}, {"PAGE", "P EY1 JH"}, {"SOUND", "S AW1 N D"},
    {"VOLUME", "V AA1 L Y UW M"}, {"STREAM", "S T R IY1 M"}, {"REBOOT", "R IY B UW1 T"},
    {"REQUIRED", "R IH K W AY1 ER D"}, {"FLASH", "F L AE1 SH"}, {"CARD", "K AA1 R D"},
    {"AUDIO", "AO1 D IY OW"}, {"TRIGGER", "T R IH1 G ER"}, {"VOICE", "V OY1 S"},
    {"MODE", "M OW1 D"}, {"CUSTOM", "K AH1 S T AX M"}, {"DROID", "D R OY1 D"},
    {"ROGER", "R AA1 JH ER"}, {"AFFIRMATIVE", "AX F ER1 M AX T IH V"},
    {"NEGATIVE", "N EH1 G AX T IH V"}, {"REBEL", "R EH1 B AX L"},
    {"EMPIRE", "EH1 M P AY ER"}, {"IMPERIAL", "IH M P IH1 R IY AX L"},
    {"CAPTAIN", "K AE1 P T AX N"}, {"MASTER", "M AE1 S T ER"},
    {"TARGET", "T AA1 R G AX T"}, {"SECTOR", "S EH1 K T ER"},
    {"ALERT", "AX L ER1 T"}, {"WARNING", "W AO1 R N IH NG"},
    {"SHIELDS", "SH IY1 L D Z"}, {"WEAPONS", "W EH1 P AX N Z"},
    {"ONLINE", "AO1 N L AY N"}, {"OFFLINE", "AO1 F L AY N"},
    {"ENABLED", "IH N EY1 B AX L D"}, {"DISABLED", "D IH S EY1 B AX L D"},
    {"ERROR", "EH1 R ER"}, {"SYSTEM", "S IH1 S T AX M"}, {"READY", "R EH1 D IY"},
    {"BOOT", "B UW1 T"}, {"CHIRP", "CH ER1 P"}, {"SWELL", "S W EH1 L"},
    {"DOING", "D UW1 IH NG"}, {"DOIN", "D UW1 IH NG"}, {"YEAH", "Y AE1"},
    {"BOO", "B UW1"}, {"YA", "Y AA1"}, {"DAFT", "D AE1 F T"}, {"PUNK", "P AH1 NG K"},
    {"HARDER", "HH AA1 R D ER"}, {"BETTER", "B EH1 T ER"},
    {"FASTER", "F AE1 S T ER"}, {"STRONGER", "S T R AO1 NG G ER"},
    {"AROUND", "AX R AW1 N D"}, {"WORLD", "W ER1 L D"},
    {"HELLO", "HH AX L OW1"}, {"GOODBYE", "G UH D B AY1"}, {"OKAY", "OW K EY1"},
    {"OK", "OW K EY1"}, {"SORRY", "S AO1 R IY"}, {"THANK", "TH AE1 NG K"},
    {"THANKS", "TH AE1 NG K S"}, {"PLEASE", "P L IY1 Z"},
    {"SYNCING", "S IH1 NG K IH NG"}, {"FILES", "F AY1 L Z"},
    {"UPDATED", "AH P D EY1 T AX D"}, {"FIRMWARE", "F ER1 M W EH R"},
    {"RESET", "R IY S EH1 T"}, {"USB", "Y UW1 EH S B IY1"},
    {"MP3", "EH M P IY TH R IY1"}, {"K2SO", "K EY1 T UW1 EH S OW1"},
    {"KYBER", "K AY1 B ER"}, {"PADAWAN", "P AE1 D AX W AA N"},
    {"SIR", "S ER1"}, {"ROBOT", "R OW1 B AA T"}, {"PROTOCOL", "P R OW1 T AX K AO L"},
    {"LOGIC", "L AA1 JH IH K"}, {"PROBLEM", "P R AA1 B L AX M"},
    {"POSSIBLE", "P AA1 S AX B AX L"}, {"PROBABLY", "P R AA1 B AX B L IY"},
    {"CONTROL", "K AX N T R OW1 L"}, {"CHECK", "CH EH1 K"},
    {"STATUS", "S T EY1 T AX S"}, {"BATTERY", "B AE1 T ER IY"},
    {"ACTIVE", "AE1 K T IH V"}, {"IDLE", "AY1 D AX L"},
    {"MUTE", "M Y UW1 T"}, {"UNMUTE", "AH N M Y UW1 T"},

    // Common & Irregular English Words
    {"THE", "DH AX"}, {"OF", "AH V"}, {"TO", "T UW"}, {"AND", "AE N D"},
    {"A", "AX"}, {"IN", "IH N"}, {"IS", "IH Z"}, {"IT", "IH T"},
    {"YOU", "Y UW1"}, {"THAT", "DH AE1 T"}, {"HE", "HH IY1"}, {"WAS", "W AA1 Z"},
    {"FOR", "F AO1 R"}, {"ON", "AA1 N"}, {"ARE", "AA1 R"}, {"AS", "AE Z"},
    {"WITH", "W IH1 DH"}, {"THEY", "DH EY1"}, {"I", "AY1"}, {"AT", "AE T"},
    {"BE", "B IY1"}, {"THIS", "DH IH1 S"}, {"HAVE", "HH AE1 V"}, {"FROM", "F R AH1 M"},
    {"OR", "AO1 R"}, {"HAD", "HH AE1 D"}, {"BY", "B AY1"}, {"WORD", "W ER1 D"},
    {"BUT", "B AH1 T"}, {"NOT", "N AA1 T"}, {"WHAT", "W AA1 T"}, {"ALL", "AO1 L"},
    {"WERE", "W ER1"}, {"WE", "W IY1"}, {"WHEN", "W EH1 N"}, {"YOUR", "Y AO1 R"},
    {"CAN", "K AE1 N"}, {"SAID", "S EH1 D"}, {"THERE", "DH EH1 R"}, {"USE", "Y UW1 Z"},
    {"WHICH", "W IH1 CH"}, {"SHE", "SH IY1"}, {"DO", "D UW1"}, {"HOW", "HH AW1"},
    {"THEIR", "DH EH1 R"}, {"IF", "IH F"}, {"WILL", "W IH1 L"}, {"UP", "AH1 P"},
    {"OTHER", "AH1 DH ER"}, {"ABOUT", "AX B AW1 T"}, {"OUT", "AW1 T"}, {"MANY", "M EH1 N IY"},
    {"THEN", "DH EH1 N"}, {"THEM", "DH EH1 M"}, {"THESE", "DH IY1 Z"}, {"SO", "S OW1"},
    {"SOME", "S AH1 M"}, {"HER", "HH ER1"}, {"WOULD", "W UH1 D"}, {"MAKE", "M EY1 K"},
    {"LIKE", "L AY1 K"}, {"HIM", "HH IH1 M"}, {"INTO", "IH1 N T UW"}, {"TIME", "T AY1 M"},
    {"HAS", "HH AE1 Z"}, {"LOOK", "L UH1 K"}, {"MORE", "M AO1 R"}, {"WRITE", "R AY1 T"},
    {"GO", "G OW1"}, {"SEE", "S IY1"}, {"NUMBER", "N AH1 M B ER"}, {"NO", "N OW1"},
    {"WAY", "W EY1"}, {"COULD", "K UH1 D"}, {"PEOPLE", "P IY1 P AX L"}, {"MY", "M AY1"},
    {"THAN", "DH AE1 N"}, {"FIRST", "F ER1 S T"}, {"WATER", "W AO1 T ER"}, {"BEEN", "B IH1 N"},
    {"CALL", "K AO1 L"}, {"WHO", "HH UW1"}, {"NOW", "N AW1"}, {"FIND", "F AY1 N D"},
    {"LONG", "L AO1 NG"}, {"DOWN", "D AW1 N"}, {"DAY", "D EY1"}, {"DID", "D IH1 D"},
    {"GET", "G EH1 T"}, {"COME", "K AH1 M"}, {"MADE", "M EY1 D"}, {"MAY", "M EY1"},
    {"PART", "P AA1 R T"}, {"OVER", "OW1 V ER"}, {"NEW", "N UW1"}, {"TAKE", "T EY1 K"},
    {"ONLY", "OW1 N L IY"}, {"LITTLE", "L IH1 T AX L"}, {"WORK", "W ER1 K"}, {"KNOW", "N OW1"},
    {"PLACE", "P L EY1 S"}, {"YEAR", "Y IH1 R"}, {"LIVE", "L IH1 V"}, {"ME", "M IY1"},
    {"BACK", "B AE1 K"}, {"GIVE", "G IH1 V"}, {"MOST", "M OW1 S T"}, {"VERY", "V EH1 R IY"},
    {"AFTER", "AE1 F T ER"}, {"THING", "TH IH1 NG"}, {"OUR", "AW1 ER"}, {"JUST", "JH AH1 S T"},
    {"NAME", "N EY1 M"}, {"GOOD", "G UH1 D"}, {"MAN", "M AE1 N"}, {"THINK", "TH IH1 NG K"},
    {"SAY", "S EY1"}, {"GREAT", "G R EY1 T"}, {"WHERE", "W EH1 R"}, {"HELP", "HH EH1 L P"},
    {"THROUGH", "TH R UW1"}, {"MUCH", "M AH1 CH"}, {"BEFORE", "B IH F AO1 R"}, {"RIGHT", "R AY1 T"},
    {"TOO", "T UW1"}, {"OLD", "OW1 L D"}, {"ANY", "EH1 N IY"}, {"SAME", "S EY1 M"},
    {"TELL", "T EH1 L"}, {"WANT", "W AA1 N T"}, {"SHOW", "SH OW1"}, {"ALSO", "AO1 L S OW"},
    {"PUT", "P UH1 T"}, {"END", "EH1 N D"}, {"DOES", "D AH1 Z"}, {"WELL", "W EH1 L"},
    {"MUST", "M AH1 S T"}, {"EVEN", "IY1 V AX N"}, {"SUCH", "S AH1 CH"}, {"BECAUSE", "B IH K AO1 Z"},
    {"TURN", "T ER1 N"}, {"HERE", "HH IH1 R"}, {"WHY", "W AY1"}, {"ASK", "AE1 S K"},
    {"READ", "R IY1 D"}, {"NEED", "N IY1 D"}, {"HOME", "HH OW1 M"}, {"US", "AH1 S"},
    {"MOVE", "M UW1 V"}, {"TRY", "T R AY1"}, {"KIND", "K AY1 N D"}, {"AGAIN", "AX G EH1 N"},
    {"CHANGE", "CH EY1 N JH"}, {"OFF", "AO1 F"}, {"PLAY", "P L EY1"}, {"AIR", "EH1 R"},
    {"AWAY", "AX W EY1"}, {"HOUSE", "HH AW1 S"}, {"POINT", "P OY1 N T"}, {"ANSWER", "AE1 N S ER"},
    {"STILL", "S T IH1 L"}, {"LEARN", "L ER1 N"}, {"SHOULD", "SH UH1 D"}, {"HIGH", "HH AY1"},
    {"EVERY", "EH1 V R IY"}, {"NEAR", "N IH1 R"}, {"OWN", "OW1 N"}, {"LAST", "L AE1 S T"},
    {"FATHER", "F AA1 DH ER"}, {"KEEP", "K IY1 P"}, {"NEVER", "N EH1 V ER"}, {"START", "S T AA1 R T"},
    {"CITY", "S IH1 T IY"}, {"EARTH", "ER1 TH"}, {"EYE", "AY1"}, {"LIGHT", "L AY1 T"},
    {"THOUGHT", "TH AO1 T"}, {"HEAD", "HH EH1 D"}, {"UNDER", "AH1 N D ER"},
    {"SURE", "SH UH1 R"}, {"CLEAR", "K L IH1 R"}, {"ORDER", "AO1 R D ER"},
    {"POWER", "P AW1 ER"}, {"FORCE", "F AO1 R S"}, {"STAR", "S T AA1 R"},
    {"STARS", "S T AA1 R Z"}, {"WAR", "W AO1 R"}, {"WARS", "W AO1 R Z"},
    {"GUARD", "G AA1 R D"}, {"COMMAND", "K AX M AE1 N D"}, {"CHANCE", "CH AE1 N S"},
    {"FAST", "F AE1 S T"}, {"PASS", "P AE1 S"}, {"CLASS", "K L AE1 S"},
    {"WATCH", "W AA1 CH"}, {"WASH", "W AA1 SH"}, {"OH", "OW1"}, {"YES", "Y EH1 S"},
    {"HEY", "HH EY1"}, {"HI", "HH AY1"}, {"BYE", "B AY1"}, {"FIRE", "F AY1 ER"},
    {"DOOR", "D AO1 R"}, {"FLOOR", "F L AO1 R"}, {"DARK", "D AA1 R K"},
    {"FAR", "F AA1 R"}, {"CAR", "K AA1 R"}, {"HEART", "HH AA1 R T"},
    {"HEARD", "HH ER1 D"}, {"FRIEND", "F R EH1 N D"}, {"FRIENDS", "F R EH1 N D Z"},
    {"LOVE", "L AH1 V"}, {"MUSIC", "M Y UW1 Z IH K"}, {"MACHINE", "M AX SH IY1 N"},
    {"HUMAN", "HH Y UW1 M AX N"}, {"LOW", "L OW1"}, {"FULL", "F UH1 L"},
    {"OPEN", "OW1 P AX N"}, {"CLOSE", "K L OW1 Z"}, {"STOP", "S T AA1 P"},
    {"WAIT", "W EY1 T"}, {"RUN", "R AH1 N"}, {"TEST", "T EH1 S T"},
    // Contractions
    {"DON'T", "D OW1 N T"}, {"CAN'T", "K AE1 N T"}, {"WON'T", "W OW1 N T"},
    {"I'M", "AY1 M"}, {"YOU'RE", "Y UH1 R"}, {"IT'S", "IH1 T S"},
    {"THAT'S", "DH AE1 T S"}, {"WHAT'S", "W AA1 T S"}, {"WE'RE", "W IH1 R"},
    {"THEY'RE", "DH EH1 R"}, {"LET'S", "L EH1 T S"}, {"ISN'T", "IH1 Z AX N T"},
    {"WASN'T", "W AA1 Z AX N T"}, {"DOESN'T", "D AH1 Z AX N T"},
    {"DIDN'T", "D IH1 D AX N T"}, {"COULDN'T", "K UH1 D AX N T"},
    {"WOULDN'T", "W UH1 D AX N T"}, {"SHOULDN'T", "SH UH1 D AX N T"},
    {"AREN'T", "AA1 R N T"}, {"WEREN'T", "W ER1 N T"}, {"HAVEN'T", "HH AE1 V AX N T"},
    {"HASN'T", "HH AE1 Z AX N T"}, {"I'LL", "AY1 L"}, {"YOU'LL", "Y UW1 L"},
    {"WE'LL", "W IY1 L"}, {"I'VE", "AY1 V"}, {"YOU'VE", "Y UW1 V"},
};

static const char* lookupException(const char* upperWord) {
    for (size_t i = 0; i < sizeof(kExceptionDict) / sizeof(kExceptionDict[0]); i++) {
        if (strcmp(kExceptionDict[i].word, upperWord) == 0) {
            return kExceptionDict[i].phones;
        }
    }
    return nullptr;
}

// ============================================================================
// Public-Domain NRL (Naval Research Lab, 1976) Letter-to-Sound Rule Engine
// ============================================================================
struct NrlRule {
    const char* left;
    const char* match;
    const char* right;
    const char* phones;
};

// Sentinel rule matches any single char as silence/fallback
static const NrlRule kRulesA[] = {
    {" ", "A", " ", "AX"}, {" ", "ARE", " ", "AA R"}, {" ", "AR", "O", "AX R"},
    {"", "AR", "#", "EH R"}, {"^", "AS", "#", "EY S"}, {"", "A", "WA", "AX"},
    {"", "AW", "", "AO"}, {" :", "ANY", "", "EH N IY"}, {"", "A", "^+#", "EY"},
    {"#:", "ALLY", "", "AX L IY"}, {" ", "AL", "#", "AX L"}, {"", "AGAIN", "", "AX G EH N"},
    {"#:", "AG", "E", "IH JH"}, {"", "A", "^+:#", "AE"}, {" :", "A", "^+ ", "EY"},
    {"", "A", "^%", "EY"}, {" ", "ARR", "", "AX R"}, {"", "ARR", "", "AE R"},
    {" :", "AR", " ", "AA R"}, {"", "AR", " ", "ER"}, {"", "AR", "", "AA R"},
    {"", "AIR", "", "EH R"}, {"", "AI", "", "EY"}, {"", "AY", "", "EY"},
    {"", "AU", "", "AO"}, {"#:", "AL", " ", "AX L"}, {"#:", "ALS", " ", "AX L Z"},
    {"", "ALK", "", "AO K"}, {"", "AL", "^", "AO L"}, {" :", "ABLE", "", "EY B AX L"},
    {"", "ABLE", "", "AX B AX L"}, {"", "ANG", "+", "EY N JH"}, {"", "A", "", "AE"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesB[] = {
    {" ", "BE", "^#", "B IH"}, {"", "BEING", "", "B IY IH NG"}, {" ", "BOTH", " ", "B OW TH"},
    {" ", "BUS", "#", "B IH Z"}, {"", "BUIL", "", "B IH L"}, {"", "B", "", "B"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesC[] = {
    {" ", "CH", "^", "K"}, {"^E", "CH", "", "K"}, {"", "CH", "", "CH"},
    {" S", "CI", "#", "S AY"}, {"", "CI", "A", "SH"}, {"", "CI", "O", "SH"},
    {"", "CI", "EN", "SH"}, {"", "C", "+", "S"}, {"", "CK", "", "K"},
    {"", "COM", "%", "K AH M"}, {"", "C", "", "K"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesD[] = {
    {"#:", "DED", " ", "D IH D"}, {".E", "D", " ", "D"}, {"#:^E", "D", " ", "T"},
    {" ", "DE", "^#", "D IH"}, {" ", "DO", " ", "D UW"}, {" ", "DOES", "", "D AH Z"},
    {" ", "DOING", "", "D UW IH NG"}, {" ", "DOW", "", "D AW"}, {"", "DU", "A", "JH UW"},
    {"", "D", "", "D"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesE[] = {
    {"#:", "E", " ", ""}, {"':^", "E", " ", ""}, {" :", "E", " ", "IY"},
    {"#", "ED", " ", "D"}, {"#:", "E", "D ", ""}, {"", "EV", "ER", "EH V"},
    {"", "E", "^%", "IY"}, {"", "ERI", "#", "IY R IY"}, {"", "ERI", "", "EH R IY"},
    {"#:", "ER", "#", "ER"}, {"", "ER", "#", "EH R"}, {"", "ER", "", "ER"},
    {" ", "EVEN", "", "IY V AX N"}, {"#:", "E", "W", ""}, {"@", "EW", "", "UW"},
    {"", "EW", "", "Y UW"}, {"", "E", "O", "IY"}, {"#:&", "ES", " ", "IH Z"},
    {"#:", "E", "S ", ""}, {"#:", "ELY", " ", "L IY"}, {"#:", "EMENT", "", "M AX N T"},
    {"", "EFUL", "", "F UH L"}, {"", "EE", "", "IY"}, {"", "EARN", "", "ER N"},
    {" ", "EAR", "^", "ER"}, {"", "EAD", "", "EH D"}, {"#:", "EA", " ", "IY AX"},
    {"", "EA", "SU", "EH"}, {"", "EA", "", "IY"}, {"", "EIGH", "", "EY"},
    {"", "EI", "", "IY"}, {" ", "EYE", "", "AY"}, {"", "EY", "", "IY"},
    {"", "EU", "", "Y UW"}, {"", "E", "", "EH"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesF[] = {
    {"", "FUL", "", "F UH L"}, {"", "F", "", "F"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesG[] = {
    {"", "GIV", "", "G IH V"}, {" ", "G", "I^", "G"}, {"", "GE", "T", "G EH"},
    {"SU", "GGES", "", "G JH EH S"}, {"", "GG", "", "G"}, {" B#", "G", "", "G"},
    {"", "G", "+", "JH"}, {"", "GREAT", "", "G R EY T"}, {"#", "GH", "", ""},
    {"", "G", "", "G"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesH[] = {
    {" ", "HAV", "", "HH AE V"}, {" ", "HERE", "", "HH IH R"}, {" ", "HOUR", "", "AW ER"},
    {"", "HOW", "", "HH AW"}, {"", "H", "#", "HH"}, {"", "H", "", ""},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesI[] = {
    {" ", "IN", "", "IH N"}, {" ", "I", " ", "AY"}, {"", "IN", "D", "AY N"},
    {"", "IER", "", "IY ER"}, {"#:R", "IED", "", "IY D"}, {"", "IED", " ", "AY D"},
    {"", "IEN", "", "IY AX N"}, {"", "IE", "T", "AY EH"}, {" :", "I", "%", "AY"},
    {"", "I", "%", "IY"}, {"", "IE", "", "IY"}, {"", "I", "^+:#", "IH"},
    {"", "IR", "#", "AY R"}, {"", "IZ", "%", "AY Z"}, {"", "IS", "%", "AY Z"},
    {"", "I", "D%", "AY"}, {"+^", "I", "^+", "IH"}, {"", "I", "T%", "AY"},
    {"#^", "I", "^+", "IH"}, {"", "I", "^+", "AY"}, {"", "IR", "", "ER"},
    {"", "IGH", "", "AY"}, {"", "ILD", "", "AY L D"}, {"", "IGN", " ", "AY N"},
    {"", "IGN", "^", "AY N"}, {"", "IGN", "%", "AY N"}, {"", "IQUE", "", "IY K"},
    {"", "I", "", "IH"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesJ[] = {
    {"", "J", "", "JH"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesK[] = {
    {" ", "K", "N", ""}, {"", "K", "", "K"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesL[] = {
    {"", "LO", "C#", "L OW"}, {"L", "L", "", ""}, {"#:^", "L", "%", "AX L"},
    {"", "LEAD", "", "L IY D"}, {"", "L", "", "L"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesM[] = {
    {"", "MOV", "", "M UW V"}, {"", "M", "", "M"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesN[] = {
    {"E", "NG", "+", "N JH"}, {"", "NG", "R", "NG G"}, {"", "NG", "#", "NG G"},
    {"", "NGL", "%", "NG G AX L"}, {"", "NG", "", "NG"}, {"", "NK", "", "NG K"},
    {" ", "NOW", " ", "N AW"}, {"", "N", "", "N"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesO[] = {
    {"", "OF", " ", "AH V"}, {"", "OROUGH", "", "ER OW"}, {"#:", "OR", " ", "ER"},
    {"#:", "ORS", " ", "ER Z"}, {"", "OR", "", "AO R"}, {" ", "ONE", "", "W AH N"},
    {"", "OW", "", "OW"}, {" ", "OVER", "", "OW V ER"}, {"", "OV", "", "AH V"},
    {"", "O", "^%", "OW"}, {"", "O", "^EN", "OW"}, {"", "O", "^I#", "OW"},
    {"", "OL", "D", "OW L"}, {"", "OUGHT", "", "AO T"}, {"", "OUGH", "", "AH F"},
    {" ", "OU", "", "AW"}, {"H", "OU", "S#", "AW"}, {"", "OUS", "", "AX S"},
    {"", "OUR", "", "AO R"}, {"", "OULD", "", "UH D"}, {"^", "OU", "^L", "AH"},
    {"", "OUP", "", "UW P"}, {"", "OU", "", "AW"}, {"", "OY", "", "OY"},
    {"", "OING", "", "OW IH NG"}, {"", "OI", "", "OY"}, {"", "OOR", "", "AO R"},
    {"", "OOK", "", "UH K"}, {"", "OOD", "", "UH D"}, {"", "OO", "", "UW"},
    {"", "O", "E", "OW"}, {"", "O", " ", "OW"}, {"", "OA", "", "OW"},
    {" ", "ONLY", "", "OW N L IY"}, {" ", "ONCE", "", "W AH N S"}, {"", "ON'T", "", "OW N T"},
    {"C", "O", "N", "AA"}, {"", "O", "NG", "AO"}, {" :^", "O", "N", "AH"},
    {"I", "ON", "", "AX N"}, {"#:", "ON", " ", "AX N"}, {"#^", "ON", "", "AX N"},
    {"", "O", "ST ", "OW"}, {"", "OF", "^", "AO F"}, {"", "OTHER", "", "AH DH ER"},
    {"", "OSS", " ", "AO S"}, {"#:^", "OM", "", "AH M"}, {"", "O", "", "AA"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesP[] = {
    {"", "PH", "", "F"}, {"", "PEOP", "", "P IY P"}, {"", "POW", "", "P AW"},
    {"", "PUT", " ", "P UH T"}, {"", "P", "", "P"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesQ[] = {
    {"", "QUAR", "", "K W AO R"}, {"", "QU", "", "K W"}, {"", "Q", "", "K"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesR[] = {
    {" ", "RE", "^#", "R IY"}, {"", "R", "", "R"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesS[] = {
    {"", "SH", "", "SH"}, {"#", "SION", "", "ZH AX N"}, {"", "SOME", "", "S AH M"},
    {"#", "SUR", "#", "ZH ER"}, {"", "SUR", "#", "SH ER"}, {"#", "SU", "#", "ZH UW"},
    {"#", "SSU", "#", "SH UW"}, {"#", "SED", " ", "Z D"}, {"#", "S", "#", "Z"},
    {"", "SAID", "", "S EH D"}, {"^", "SION", "", "SH AX N"}, {"", "S", "S", ""},
    {".", "S", " ", "Z"}, {"#:.E", "S", " ", "Z"}, {"#:^##", "S", " ", "Z"},
    {"#:^#", "S", " ", "S"}, {"U", "S", " ", "S"}, {" :#", "S", " ", "Z"},
    {" ", "SCH", "", "S K"}, {"", "S", "C+", ""}, {"#", "SM", "", "Z M"},
    {"#", "SN", "'", "Z AX N"}, {"", "S", "", "S"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesT[] = {
    {" ", "THE", " ", "DH AX"}, {"", "TO", " ", "T UW"}, {"", "THAT", " ", "DH AE T"},
    {" ", "THIS", " ", "DH IH S"}, {" ", "THEY", "", "DH EY"}, {" ", "THERE", "", "DH EH R"},
    {"", "THER", "", "DH ER"}, {"", "THEIR", "", "DH EH R"}, {" ", "THAN", " ", "DH AE N"},
    {" ", "THEM", " ", "DH EH M"}, {"", "THESE", " ", "DH IY Z"}, {" ", "THEN", "", "DH EH N"},
    {"", "THROUGH", "", "TH R UW"}, {"", "THOSE", "", "DH OW Z"}, {"", "THOUGH", " ", "DH OW"},
    {" ", "THUS", "", "DH AH S"}, {"", "TH", "", "TH"}, {"#:", "TED", " ", "T IH D"},
    {"S", "TI", "#N", "CH"}, {"", "TI", "O", "SH"}, {"", "TI", "A", "SH"},
    {"", "TIEN", "", "SH AX N"}, {"", "TUR", "#", "CH ER"}, {"", "TU", "A", "CH UW"},
    {" ", "TWO", "", "T UW"}, {"", "T", "", "T"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesU[] = {
    {" ", "UN", "I", "Y UW N"}, {" ", "UN", "", "AH N"}, {" ", "UPON", "", "AX P AO N"},
    {"@", "UR", "#", "UH R"}, {"", "UR", "#", "Y UH R"}, {"", "UR", "", "ER"},
    {"", "U", "^ ", "AH"}, {"", "U", "^^", "AH"}, {"", "UY", "", "AY"},
    {" G", "U", "#", ""}, {"G", "U", "%", ""}, {"G", "U", "#", "W"},
    {"#N", "U", "", "Y UW"}, {"@", "U", "", "UW"}, {"", "U", "", "Y UW"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesV[] = {
    {"", "VIEW", "", "V Y UW"}, {"", "V", "", "V"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesW[] = {
    {" ", "WERE", "", "W ER"}, {"", "WA", "S", "W AA"}, {"", "WA", "T", "W AA"},
    {"", "WHERE", "", "W EH R"}, {"", "WHAT", "", "W AA T"}, {"", "WHOL", "", "HH OW L"},
    {"", "WHO", "", "HH UW"}, {"", "WH", "", "W"}, {"", "WAR", "", "W AO R"},
    {"", "WOR", "^", "W ER"}, {"", "WR", "", "R"}, {"", "W", "", "W"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesX[] = {
    {"", "X", "", "K S"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesY[] = {
    {"", "YOUNG", "", "Y AH NG"}, {" ", "YOU", "", "Y UW"}, {" ", "YES", "", "Y EH S"},
    {" ", "Y", "", "Y"}, {"#:^", "Y", " ", "IY"}, {"#:^", "Y", "I", "IY"},
    {" :", "Y", " ", "AY"}, {" :", "Y", "#", "AY"}, {" :", "Y", "^+:#", "IH"},
    {" :", "Y", "^#", "AY"}, {"", "Y", "", "IH"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule kRulesZ[] = {
    {"", "Z", "", "Z"},
    {nullptr, nullptr, nullptr, nullptr}
};

static const NrlRule* const kRulesByLetter[26] = {
    kRulesA, kRulesB, kRulesC, kRulesD, kRulesE, kRulesF, kRulesG,
    kRulesH, kRulesI, kRulesJ, kRulesK, kRulesL, kRulesM, kRulesN,
    kRulesO, kRulesP, kRulesQ, kRulesR, kRulesS, kRulesT, kRulesU,
    kRulesV, kRulesW, kRulesX, kRulesY, kRulesZ
};

static bool nrlIsVowel(char c) {
    return (c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y');
}

static bool nrlIsConsonant(char c) {
    return (c >= 'A' && c <= 'Z' && !nrlIsVowel(c));
}

static bool nrlIsVoiced(char c) {
    return (c == 'B' || c == 'D' || c == 'V' || c == 'G' || c == 'J' ||
            c == 'L' || c == 'M' || c == 'N' || c == 'R' || c == 'W' || c == 'Z');
}

static bool nrlIsFrontVowel(char c) {
    return (c == 'E' || c == 'I' || c == 'Y');
}

static bool nrlIsPalatalTrigger(char c) {
    // '@' in classic NRL rules: T, S, R, D, L, Z, N, J, CH, SH, TH
    return (c == 'T' || c == 'S' || c == 'R' || c == 'D' || c == 'L' ||
            c == 'Z' || c == 'N' || c == 'J');
}

static bool nrlIsSibilant(char c) {
    // '&' in classic NRL rules: S, C, G, Z, X, J
    return (c == 'S' || c == 'C' || c == 'G' || c == 'Z' || c == 'X' || c == 'J');
}

static bool matchLeftRec(const char* pat, int pi, const char* text, int ti) {
    if (pi < 0) return true;
    char pc = pat[pi];

    if (pc == '#') {
        if (ti < 0 || !nrlIsVowel(text[ti])) return false;
        int k = 1;
        while (ti - k + 1 >= 0 && nrlIsVowel(text[ti - k + 1])) {
            if (matchLeftRec(pat, pi - 1, text, ti - k)) return true;
            k++;
        }
        return false;
    }
    if (pc == ':') {
        if (matchLeftRec(pat, pi - 1, text, ti)) return true;
        int k = 1;
        while (ti - k + 1 >= 0 && nrlIsConsonant(text[ti - k + 1])) {
            if (matchLeftRec(pat, pi - 1, text, ti - k)) return true;
            k++;
        }
        return false;
    }
    if (pc == '^') {
        return (ti >= 0 && nrlIsConsonant(text[ti]) && matchLeftRec(pat, pi - 1, text, ti - 1));
    }
    if (pc == '.') {
        return (ti >= 0 && nrlIsVoiced(text[ti]) && matchLeftRec(pat, pi - 1, text, ti - 1));
    }
    if (pc == '+') {
        return (ti >= 0 && nrlIsFrontVowel(text[ti]) && matchLeftRec(pat, pi - 1, text, ti - 1));
    }
    if (pc == '@') {
        if (ti >= 0 && nrlIsPalatalTrigger(text[ti]) && matchLeftRec(pat, pi - 1, text, ti - 1)) return true;
        if (ti >= 1 && text[ti] == 'H' && (text[ti - 1] == 'C' || text[ti - 1] == 'S' || text[ti - 1] == 'T')) {
            return matchLeftRec(pat, pi - 1, text, ti - 2);
        }
        return false;
    }
    if (pc == '&') {
        if (ti >= 0 && nrlIsSibilant(text[ti]) && matchLeftRec(pat, pi - 1, text, ti - 1)) return true;
        if (ti >= 1 && text[ti] == 'H' && (text[ti - 1] == 'C' || text[ti - 1] == 'S')) {
            return matchLeftRec(pat, pi - 1, text, ti - 2);
        }
        return false;
    }
    return (ti >= 0 && text[ti] == pc && matchLeftRec(pat, pi - 1, text, ti - 1));
}

static bool matchRightRec(const char* pat, int pi, const char* text, int ti, int totalLen) {
    char pc = pat[pi];
    if (pc == '\0') return true;

    if (pc == '#') {
        if (ti >= totalLen || !nrlIsVowel(text[ti])) return false;
        int k = 1;
        while (ti + k - 1 < totalLen && nrlIsVowel(text[ti + k - 1])) {
            if (matchRightRec(pat, pi + 1, text, ti + k, totalLen)) return true;
            k++;
        }
        return false;
    }
    if (pc == ':') {
        if (matchRightRec(pat, pi + 1, text, ti, totalLen)) return true;
        int k = 1;
        while (ti + k - 1 < totalLen && nrlIsConsonant(text[ti + k - 1])) {
            if (matchRightRec(pat, pi + 1, text, ti + k, totalLen)) return true;
            k++;
        }
        return false;
    }
    if (pc == '^') {
        return (ti < totalLen && nrlIsConsonant(text[ti]) && matchRightRec(pat, pi + 1, text, ti + 1, totalLen));
    }
    if (pc == '.') {
        return (ti < totalLen && nrlIsVoiced(text[ti]) && matchRightRec(pat, pi + 1, text, ti + 1, totalLen));
    }
    if (pc == '+') {
        return (ti < totalLen && nrlIsFrontVowel(text[ti]) && matchRightRec(pat, pi + 1, text, ti + 1, totalLen));
    }
    if (pc == '%') {
        // ER, ES, ED, ELY, ING, E
        if (ti + 3 <= totalLen && (strncmp(&text[ti], "ELY", 3) == 0 || strncmp(&text[ti], "ING", 3) == 0)) {
            if (matchRightRec(pat, pi + 1, text, ti + 3, totalLen)) return true;
        }
        if (ti + 2 <= totalLen && (strncmp(&text[ti], "ER", 2) == 0 || strncmp(&text[ti], "ES", 2) == 0 ||
                                   strncmp(&text[ti], "ED", 2) == 0)) {
            if (matchRightRec(pat, pi + 1, text, ti + 2, totalLen)) return true;
        }
        if (ti < totalLen && text[ti] == 'E') {
            if (matchRightRec(pat, pi + 1, text, ti + 1, totalLen)) return true;
        }
        return false;
    }
    return (ti < totalLen && text[ti] == pc && matchRightRec(pat, pi + 1, text, ti + 1, totalLen));
}

static bool hasUnstressedPrefix(const char* upperWord) {
    static const char* const kPrefixes[] = {
        "RE", "DE", "BE", "PRE", "PRO", "UN", "IN", "IM", "DIS",
        "CON", "COM", "EX", "EN", "EM", "FOR", "PER", "SUB", "TRANS"
    };
    size_t len = strlen(upperWord);
    if (len < 4) return false;
    for (size_t i = 0; i < sizeof(kPrefixes) / sizeof(kPrefixes[0]); i++) {
        size_t plen = strlen(kPrefixes[i]);
        if (len > plen + 2 && strncmp(upperWord, kPrefixes[i], plen) == 0) {
            return true;
        }
    }
    // Single-letter 'A-' before consonant in multi-syllable words (about, alert, around)
    if (upperWord[0] == 'A' && nrlIsConsonant(upperWord[1]) && len >= 5) {
        return true;
    }
    return false;
}

static void translateWordNrl(const char* upperWord, KlattPhoneme* out, int& outCount, int maxPhones) {
    char padded[68];
    int wordLen = (int)strlen(upperWord);
    if (wordLen <= 0) return;
    if (wordLen > 64) wordLen = 64;

    padded[0] = ' ';
    memcpy(&padded[1], upperWord, (size_t)wordLen);
    padded[wordLen + 1] = ' ';
    padded[wordLen + 2] = '\0';
    int totalLen = wordLen + 2;

    int startIdx = outCount;
    int pos = 1;
    while (pos <= wordLen && outCount < maxPhones) {
        char ch = padded[pos];
        if (ch < 'A' || ch > 'Z') {
            pos++;
            continue;
        }
        const NrlRule* rules = kRulesByLetter[ch - 'A'];
        int advance = 1;
        for (int r = 0; rules[r].match != nullptr; r++) {
            int mLen = (int)strlen(rules[r].match);
            if (pos + mLen > totalLen) continue;
            if (strncmp(&padded[pos], rules[r].match, (size_t)mLen) != 0) continue;

            int leftLen = (int)strlen(rules[r].left);
            if (!matchLeftRec(rules[r].left, leftLen - 1, padded, pos - 1)) continue;
            if (!matchRightRec(rules[r].right, 0, padded, pos + mLen, totalLen)) continue;

            appendPhoneString(rules[r].phones, (outCount == startIdx), out, outCount, maxPhones);
            advance = mLen;
            break;
        }
        pos += advance;
    }

    // Assign primary lexical stress to unknown words
    int vowelIdxs[24];
    int numVowels = 0;
    for (int i = startIdx; i < outCount && numVowels < 24; i++) {
        if (isKlattVowel(out[i].id)) {
            vowelIdxs[numVowels++] = i;
        }
    }
    if (numVowels > 0) {
        int stressSlot = 0;
        if (numVowels >= 2 && hasUnstressedPrefix(upperWord)) {
            stressSlot = 1;
        }
        out[vowelIdxs[stressSlot]].stress = 1;
    }
}

static bool isCommonTrapNWord(const char* w) {
    static const char* const kTrapN[] = {
        "AND", "HAND", "HANDS", "LAND", "STAND", "BAND", "SAND", "BRAND",
        "GRAND", "PLAN", "MAN", "CAN", "RAN", "THAN", "SCAN", "BANK",
        "TANK", "THANK", "THANKS"
    };
    for (size_t i = 0; i < sizeof(kTrapN) / sizeof(kTrapN[0]); i++) {
        if (strcmp(w, kTrapN[i]) == 0) return true;
    }
    return false;
}

static bool isLotRoundedWord(const char* w) {
    if (!w || !w[0]) return false;
    for (const char* p = w; *p; p++) {
        if (*p == 'O') return true;
    }
    return (strncmp(w, "WA", 2) == 0 || strncmp(w, "WH", 2) == 0 ||
            strncmp(w, "QUA", 3) == 0 || strncmp(w, "SQUA", 4) == 0);
}

static bool isSilentSuffixStops(uint8_t id) {
    return (id == PH_P || id == PH_T || id == PH_K || id == PH_F ||
            id == PH_TH || id == PH_S || id == PH_SH || id == PH_CH);
}

// ============================================================================
// British RP Post-Lexical Phoneme Rewrite Pass
// ============================================================================
static constexpr int MAX_WORD_STORAGE = 80;
static constexpr int MAX_WORD_CHARS = 20;

static void applyBritishRpRules(KlattPhoneme* phones, int& count,
                                const char wordBuf[MAX_WORD_STORAGE][MAX_WORD_CHARS],
                                const uint8_t* wordIdxForPhone) {
    // Pass 1: Vowel quality shifts (LOT rounding, TRAP-BATH split, NURSE/LETTER non-rhotic vowels)
    for (int i = 0; i < count; i++) {
        uint8_t id = phones[i].id;
        if (id == PH_SIL) continue;

        uint8_t nextId = (i + 1 < count) ? phones[i + 1].id : PH_SIL;
        uint8_t next2Id = (i + 2 < count) ? phones[i + 2].id : PH_SIL;
        uint8_t wIdx = wordIdxForPhone[i];
        const char* w = (wIdx < MAX_WORD_STORAGE) ? wordBuf[wIdx] : "";
        bool nextSameWord = (i + 1 < count) && !(phones[i + 1].flags & KLATT_FL_WORD_START) && (nextId != PH_SIL);
        bool next2SameWord = (i + 2 < count) && !(phones[i + 2].flags & KLATT_FL_WORD_START) && (next2Id != PH_SIL);

        // Fronted British RP GOAT diphthong [əʊ]
        if (id == PH_OW) {
            phones[i].flags |= KLATT_FL_BRITISH_OW;
        }

        // LOT / CLOTH rounding: AA (without following R) -> AO in O/WA/WH/QUA words
        if (id == PH_AA && nextId != PH_R && w[0] && isLotRoundedWord(w)) {
            phones[i].id = PH_AO;
            continue;
        }

        // TRAP-BATH split: AE -> broad British long AA before s/f/th or n+s/t/d/ch
        if (id == PH_AE && nextSameWord && w[0]) {
            bool isBath = false;
            if (nextId == PH_S || nextId == PH_F || nextId == PH_TH) {
                if (!next2SameWord || next2Id == PH_T || next2Id == PH_P || next2Id == PH_K) {
                    isBath = true;
                }
            } else if (nextId == PH_N && !isCommonTrapNWord(w)) {
                if (next2SameWord && (next2Id == PH_S || next2Id == PH_T || next2Id == PH_D || next2Id == PH_CH)) {
                    isBath = true;
                }
            }
            if (isBath) {
                phones[i].id = PH_AA;
                phones[i].flags |= KLATT_FL_LONG;
                continue;
            }
        }

        // Non-rhotic ER (LETTER -> short schwa AX; stressed NURSE -> long central AX)
        if (id == PH_ER && !isKlattVowel(nextId)) {
            if (phones[i].stress > 0) {
                phones[i].id = PH_AX;
                phones[i].flags |= KLATT_FL_LONG;
            } else {
                phones[i].id = PH_AX;
            }
            continue;
        }
    }

    // Pass 2: Non-rhotic post-vocalic R dropping / centering glide (preserves linking-R before vowels)
    int i = 0;
    while (i < count) {
        if (phones[i].id == PH_R && i > 0 && !(phones[i].flags & KLATT_FL_WORD_START)) {
            uint8_t prevId = phones[i - 1].id;
            uint8_t nextId = (i + 1 < count) ? phones[i + 1].id : PH_SIL;

            if (isKlattVowel(prevId) && !isKlattVowel(nextId)) {
                if (prevId == PH_AA || prevId == PH_AO || prevId == PH_AH || prevId == PH_AX) {
                    // START / NORTH / FORCE: lengthen preceding vowel and drop 'R'
                    phones[i - 1].flags |= KLATT_FL_LONG;
                    memmove(&phones[i], &phones[i + 1], (size_t)(count - i - 1) * sizeof(KlattPhoneme));
                    count--;
                    continue;
                } else {
                    // NEAR / SQUARE / CURE: replace 'R' with subtle centering schwa glide
                    phones[i].id = PH_AX;
                    phones[i].stress = 0;
                    phones[i].flags |= KLATT_FL_GLIDE;
                }
            }
        }
        i++;
    }
}

static bool tryAppendStemPlural(const char* tok, int stemLen, KlattPhoneme* outPhones,
                                int& count, int maxPhones) {
    char stem[64];
    memcpy(stem, tok, (size_t)stemLen);
    stem[stemLen] = '\0';
    const char* stemPh = lookupException(stem);
    if (!stemPh) return false;

    int beforeCount = count;
    appendPhoneString(stemPh, true, outPhones, count, maxPhones - 1);
    if (count > beforeCount && count < maxPhones) {
        uint8_t lastId = outPhones[count - 1].id;
        uint8_t pluralId = isSilentSuffixStops(lastId) ? PH_S : PH_Z;
        outPhones[count++] = {pluralId, 0, KLATT_FL_NONE};
    }
    return true;
}

// ============================================================================
// English Number & Ordinal Expansion
// ============================================================================
static const char* const kNumOnes[20] = {
    "", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
    "sixteen", "seventeen", "eighteen", "nineteen"
};

static const char* const kNumTens[10] = {
    "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
};

static const char* const kNumOnesOrd[20] = {
    "", "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth",
    "tenth", "eleventh", "twelfth", "thirteenth", "fourteenth", "fifteenth",
    "sixteenth", "seventeenth", "eighteenth", "nineteenth"
};

static const char* const kNumTensOrd[10] = {
    "", "", "twentieth", "thirtieth", "fortieth", "fiftieth",
    "sixtieth", "seventieth", "eightieth", "ninetieth"
};

static void appendNumWord(char* dst, size_t dstSize, const char* w) {
    if (!dst || dstSize == 0 || !w || w[0] == '\0') return;
    size_t curLen = strlen(dst);
    if (curLen > 0 && curLen + 1 < dstSize) {
        dst[curLen++] = ' ';
        dst[curLen] = '\0';
    }
    if (curLen + 1 < dstSize) {
        strncat(dst, w, dstSize - curLen - 1);
    }
}

static void appendBelowThousandCardinal(char* dst, size_t dstSize, int v) {
    if (v >= 100) {
        appendNumWord(dst, dstSize, kNumOnes[v / 100]);
        appendNumWord(dst, dstSize, "hundred");
        v %= 100;
    }
    if (v >= 20) {
        appendNumWord(dst, dstSize, kNumTens[v / 10]);
        v %= 10;
    }
    if (v > 0) {
        appendNumWord(dst, dstSize, kNumOnes[v]);
    }
}

void klattNumberToWords(uint64_t n, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (n == 0) {
        strncpy(out, "zero", outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }
    if (n >= 1000000000000ULL) {
        char digits[24];
        snprintf(digits, sizeof(digits), "%llu", (unsigned long long)n);
        for (int i = 0; digits[i]; i++) {
            int d = digits[i] - '0';
            appendNumWord(out, outSize, (d == 0) ? "zero" : kNumOnes[d]);
        }
        return;
    }
    if (n >= 1000000000ULL) {
        appendBelowThousandCardinal(out, outSize, (int)(n / 1000000000ULL));
        appendNumWord(out, outSize, "billion");
        n %= 1000000000ULL;
    }
    if (n >= 1000000ULL) {
        appendBelowThousandCardinal(out, outSize, (int)(n / 1000000ULL));
        appendNumWord(out, outSize, "million");
        n %= 1000000ULL;
    }
    if (n >= 1000ULL) {
        appendBelowThousandCardinal(out, outSize, (int)(n / 1000ULL));
        appendNumWord(out, outSize, "thousand");
        n %= 1000ULL;
    }
    if (n > 0) {
        appendBelowThousandCardinal(out, outSize, (int)n);
    }
}

void klattOrdinalToWords(uint64_t n, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (n == 0) {
        strncpy(out, "zeroth", outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }
    if (n >= 1000000000000ULL) {
        klattNumberToWords(n, out, outSize);
        return;
    }
    if (n >= 1000000000ULL) {
        appendBelowThousandCardinal(out, outSize, (int)(n / 1000000000ULL));
        n %= 1000000000ULL;
        if (n == 0) {
            appendNumWord(out, outSize, "billionth");
            return;
        }
        appendNumWord(out, outSize, "billion");
    }
    if (n >= 1000000ULL) {
        appendBelowThousandCardinal(out, outSize, (int)(n / 1000000ULL));
        n %= 1000000ULL;
        if (n == 0) {
            appendNumWord(out, outSize, "millionth");
            return;
        }
        appendNumWord(out, outSize, "million");
    }
    if (n >= 1000ULL) {
        appendBelowThousandCardinal(out, outSize, (int)(n / 1000ULL));
        n %= 1000ULL;
        if (n == 0) {
            appendNumWord(out, outSize, "thousandth");
            return;
        }
        appendNumWord(out, outSize, "thousand");
    }
    int v = (int)n;
    if (v >= 100) {
        appendNumWord(out, outSize, kNumOnes[v / 100]);
        v %= 100;
        if (v == 0) {
            appendNumWord(out, outSize, "hundredth");
            return;
        }
        appendNumWord(out, outSize, "hundred");
    }
    if (v >= 20) {
        if (v % 10 == 0) {
            appendNumWord(out, outSize, kNumTensOrd[v / 10]);
            return;
        }
        appendNumWord(out, outSize, kNumTens[v / 10]);
        v %= 10;
    }
    if (v > 0) {
        appendNumWord(out, outSize, kNumOnesOrd[v]);
    }
}

// ============================================================================
// Public Text -> Phoneme Front-End
// ============================================================================
int klattTextToPhonemes(const char* text, bool britishRp, KlattPhoneme* outPhones, int maxPhones) {
    if (!text || !outPhones || maxPhones < 4) return 0;

    char wordBuf[MAX_WORD_STORAGE][MAX_WORD_CHARS];
    uint8_t wordIdxForPhone[KLATT_MAX_PHONEMES];
    int wordSlot = 0;
    int count = 0;

    // Initial short lead-in silence
    outPhones[count] = {PH_SIL, 0, KLATT_FL_NONE};
    wordIdxForPhone[count] = 0xFF;
    count++;

    const char* p = text;
    while (*p && count < maxPhones - 2) {
        while (*p && !isalnum((unsigned char)*p) && *p != '\'' &&
               *p != '.' && *p != '!' && *p != '?' && *p != ',' && *p != ';' && *p != ':') {
            p++;
        }
        if (!*p) break;

        // Punctuation pause
        if (*p == '.' || *p == '!' || *p == '?' || *p == ',' || *p == ';' || *p == ':') {
            uint8_t pFlag = KLATT_FL_COMMA;
            if (*p == '?') pFlag = KLATT_FL_CLAUSE_END | KLATT_FL_QUESTION;
            else if (*p == '.' || *p == '!' || *p == ';') pFlag = KLATT_FL_CLAUSE_END;

            if (count > 0 && outPhones[count - 1].id == PH_SIL) {
                outPhones[count - 1].flags |= pFlag;
            } else if (count < maxPhones - 1) {
                outPhones[count] = {PH_SIL, 0, pFlag};
                wordIdxForPhone[count] = 0xFF;
                count++;
            }
            p++;
            continue;
        }

        // Collect word token (uppercase letters, digits, or apostrophe)
        char tok[64];
        int tLen = 0;
        bool allDigits = true;
        while (*p && (isalnum((unsigned char)*p) || *p == '\'') && tLen < (int)sizeof(tok) - 1) {
            char up = (char)toupper((unsigned char)*p++);
            if (!isdigit((unsigned char)up)) allDigits = false;
            tok[tLen++] = up;
        }
        tok[tLen] = '\0';
        if (tLen == 0) continue;

        uint8_t curWordIdx = 0xFF;
        if (wordSlot < MAX_WORD_STORAGE) {
            strncpy(wordBuf[wordSlot], tok, MAX_WORD_CHARS - 1);
            wordBuf[wordSlot][MAX_WORD_CHARS - 1] = '\0';
            curWordIdx = (uint8_t)wordSlot++;
        }

        int beforeCount = count;
        const char* dictPhones = lookupException(tok);
        if (dictPhones) {
            appendPhoneString(dictPhones, true, outPhones, count, maxPhones - 1);
        } else if (allDigits) {
            if (tLen <= 12) {
                uint64_t val = 0;
                for (int di = 0; di < tLen; di++) {
                    val = val * 10ULL + (uint64_t)(tok[di] - '0');
                }
                char numWords[192];
                klattNumberToWords(val, numWords, sizeof(numWords));
                const char* wp = numWords;
                while (*wp && count < maxPhones - 1) {
                    while (*wp == ' ') wp++;
                    if (!*wp) break;
                    char upWord[20];
                    int ui = 0;
                    while (*wp && *wp != ' ') {
                        if (ui < (int)sizeof(upWord) - 1) {
                            upWord[ui++] = (char)toupper((unsigned char)*wp);
                        }
                        wp++;
                    }
                    upWord[ui] = '\0';
                    const char* wPh = lookupException(upWord);
                    if (wPh) {
                        appendPhoneString(wPh, true, outPhones, count, maxPhones - 1);
                    }
                }
            } else {
                // Speak very long digit strings digit by digit
                for (int di = 0; di < tLen && count < maxPhones - 1; di++) {
                    char dStr[2] = {tok[di], '\0'};
                    const char* dPh = lookupException(dStr);
                    if (dPh) {
                        appendPhoneString(dPh, true, outPhones, count, maxPhones - 1);
                    }
                }
            }
        } else {
            bool handled = false;
            if (tLen > 2 && tok[tLen - 2] == '\'' && tok[tLen - 1] == 'S') {
                handled = tryAppendStemPlural(tok, tLen - 2, outPhones, count, maxPhones - 1);
            } else if (tLen > 3 && tok[tLen - 1] == 'S' && tok[tLen - 2] != 'S') {
                handled = tryAppendStemPlural(tok, tLen - 1, outPhones, count, maxPhones - 1);
            }
            if (!handled) {
                translateWordNrl(tok, outPhones, count, maxPhones - 1);
            }
        }

        for (int i = beforeCount; i < count; i++) {
            wordIdxForPhone[i] = curWordIdx;
        }
    }

    if (britishRp) {
        applyBritishRpRules(outPhones, count, wordBuf, wordIdxForPhone);
    }

    // Ensure final silence with terminal clause marker
    if (count > 0 && outPhones[count - 1].id == PH_SIL) {
        outPhones[count - 1].flags |= KLATT_FL_CLAUSE_END;
    } else if (count < maxPhones) {
        outPhones[count] = {PH_SIL, 0, KLATT_FL_CLAUSE_END};
        count++;
    }

    return count;
}
