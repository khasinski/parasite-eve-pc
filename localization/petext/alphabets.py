"""Every language the project means to support: its letters and a line of its own to test with.

The one list of what a language needs. `pelang.py languages` reports which ones the pack tool can
encode today, and the tests keep that report true. Lowercase letters are listed once; capitals are
taken with str.upper(), except where a language's capitals differ (`upper`, `no_upper`). `sample`
is a pangram or a plain sentence written for testing, never game text.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Language:
    code: str
    name: str
    letters: str                # beyond a-z when latin, the whole alphabet otherwise
    sample: str
    latin: bool = True
    upper: str = ""             # capitals to add by hand
    no_upper: str = ""          # letters with no capital of their own (ß, ı)


LANGUAGES = [
    # Plain Latin letters.
    Language("en", "English", "", "The quick brown fox jumps over the lazy dog."),
    Language("id", "Indonesian", "", "Muharjo seorang xenofobia universal yang takut pada warga jazirah."),
    Language("ms", "Malay", "", "Saya suka makan nasi lemak di kedai kopi setiap pagi."),
    Language("sw", "Swahili", "", "Tazama jinsi mbweha mwepesi anavyoruka juu ya mbwa mvivu."),
    Language("tl", "Filipino", "ñ", "Ang bawat tao ay isinilang na malaya, señor. Kumusta po kayo?"),
    # Latin letters with accents, from the Latin Extended set.
    Language("es", "Spanish", "áéíóúüñ", "¿Qué fue? ¡El pingüino Wenceslao hizo kilómetros bajo exhaustiva lluvia y frío, añoraba a su querido cachorro!"),
    Language("pt", "Portuguese", "áâãàçéêíóôõúü", "Luís argüia à Júlia que «brações, fé, chá, óxido, pôr, zângão» eram palavras do português."),
    Language("fr", "French", "àâæçéèêëîïôœùûüÿ", "Dès Noël, où un zéphyr haï me vêt de glaçons würmiens, je dîne d'exquis rôtis de bœuf au kir, à l'aÿ d'âge mûr."),
    Language("de", "German", "äöüß", "Zwölf Boxkämpfer jagen Viktor quer über den großen Sylter Deich.", no_upper="ß"),
    Language("it", "Italian", "àèéìíîòóùú", "Pranzo d'acqua fa volti sghembi: più è così, perché già né può città."),
    Language("nl", "Dutch", "áéíóúëïöü", "Pa's wijze lynx bezag vroom het fikse aquaduct. Eén coördinatie, geïnd, ruïne, reünie."),
    Language("sv", "Swedish", "åäö", "Flygande bäckasiner söka hwila på mjuka tuvor."),
    Language("nb", "Norwegian", "æøå", "Vår sære Zulu fra badeøya spilte jo whist og quickstep i min taxi."),
    Language("da", "Danish", "æøå", "Quizdeltagerne spiste jordbær med fløde, mens cirkusklovnen Wolther spillede på xylofon."),
    Language("fi", "Finnish", "åäöšž", "Törkylempijävongahdus: albert osti fagotin ja töräytti puhkuvan melodian. Šekki, Žilina, Åbo."),
    Language("pl", "Polish", "ąćęłńóśźż", "Pchnąć w tę łódź jeża lub ośm skrzyń fig. Zażółć gęślą jaźń."),
    Language("tr", "Turkish", "çğıöşü", "Pijamalı hasta yağız şoföre çabucak güvendi. İstanbul, IĞDIR.", upper="İ", no_upper="ı"),
    Language("cs", "Czech", "áčďéěíňóřšťúůýž", "Příliš žluťoučký kůň úpěl ďábelské ódy. Nechť již hříšné saxofony ďáblů rozezvučí síň."),
    Language("sk", "Slovak", "áäčďéíĺľňóôŕšťúýž", "Kŕdeľ šťastných ďatľov učí pri ústí Váhu mĺkveho koňa obhrýzať kôru a žrať čerstvé mäso."),
    Language("hu", "Hungarian", "áéíóöőúüű", "Árvíztűrő tükörfúrógép. Jó foxim és don Quijote húszwattos lámpánál ülve egy pár bűvös cipőt készít."),
    Language("ca", "Catalan", "àçèéíïòóúü", "Jove xef, porti whisky amb quinze glaçons d'hidrogen, coi! Això és més fàcil, veí, adéu."),
    Language("sq", "Albanian", "çë", "Unë njoh një çift që flet shqip dhe xhiron një film në Gjakovë."),
    Language("et", "Estonian", "äõöüšž", "Põdur Zagrebi tšellomängija-följetonist Ciqo külmetas kehvas garaažis."),
    # Latin letters past the fixed codes: a pack's own table holds them (petext/letters.py).
    Language("ro", "Romanian", "ăâîșț", "Muzicologă în bej vând whisky și tequila, preț fix. Țară, știință, câmp."),
    Language("hr", "Croatian", "čćđšž", "Gojazni đačić s biciklom drži hmelj i finu vatu u džepu nošnje."),
    Language("sr-Latn", "Serbian (Latin)", "čćđšž", "Ljubazni fenjerdžija čađavog lica hoće da mi pokaže štos."),
    Language("sl", "Slovenian", "čšž", "Hišničin bratec vzgaja polže pod fikusom."),
    Language("is", "Icelandic", "áðéíóúýþæö", "Kæmi ný öxi hér, ykist þjófum nú bæði víl og ádrepa."),
    Language("lt", "Lithuanian", "ąčęėįšųūž", "Įlinkdama fechtuotojo špaga sublykčiojusi pragręžė apvalų arbūzą."),
    Language("lv", "Latvian", "āčēģīķļņšūž", "Glāžšķūņa rūķīši dzērumā čiepj Baha koncertflīģeļu vākus."),
    Language("vi", "Vietnamese", "àáảãạăằắẳẵặâầấẩẫậđèéẻẽẹêềếểễệìíỉĩịòóỏõọôồốổỗộơờớởỡợùúủũụưừứửữựỳýỷỹỵ",
             "Tiếng Việt: một con cáo nâu nhanh nhẹn nhảy qua con chó lười biếng."),
    # Cyrillic.
    Language("ru", "Russian", "абвгдеёжзийклмнопрстуфхцчшщъыьэюя", "Съешь же ещё этих мягких французских булок, да выпей чаю.", latin=False),
    Language("uk", "Ukrainian", "абвгґдеєжзиіїйклмнопрстуфхцчшщьюя", "Чуєш їх, доцю, га? Кумедна ж ти, прощайся без ґольфів!", latin=False),
    Language("be", "Belarusian", "абвгдеёжзійклмнопрстуўфхцчшыьэюя", "У Іўі худы жвавы чорт у зялёнай камізэльцы пабег пад'есці фаршу з юшкай.", latin=False),
    Language("bg", "Bulgarian", "абвгдежзийклмнопрстуфхцчшщъьюя", "Ах, чудна българска земьо, полюшвай цъфтящи жита.", latin=False),
    Language("sr-Cyrl", "Serbian (Cyrillic)", "абвгдђежзијклљмнњопрстћуфхцчџш", "Љубазни фењерџија чађавог лица хоће да ми покаже штос.", latin=False),
    Language("mk", "Macedonian", "абвгдѓежзѕијклљмнњопрстќуфхцчџш", "Ѕидарски пејзаж: шугав билмез со чудење џвака ќофте и кељ на туѓ цех.", latin=False),
    Language("kk", "Kazakh", "аәбвгғдеёжзийкқлмнңоөпрстуұүфхһцчшщъыіьэюя", "Қазақ тілінде сөйлеуді үйрену өте қызықты және пайдалы, әрине.", latin=False),
    # Greek.
    Language("el", "Greek", "αβγδεζηθικλμνξοπρστυφχψωάέήίόύώϊϋΐΰς", "Ξεσκεπάζω την ψυχοφθόρα βδελυγμία.", latin=False, no_upper="ςΐΰ"),
    # Chinese, Japanese, Korean: letters come per pack, so these are a sample rather than an alphabet.
    Language("zh-Hans", "Chinese (Simplified)", "的一是在不了有和人这中大为上个国我以要他时来用们生到作地于出就分对成会可主发年动同工也能下过子说产种面而方后多定行学法所民得经十三之进着等部度家电力里如水化高自二理起小物现实加量都两体制机当使点从业本去把性好应开它合还因由其些然前外天政四日那社义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命此变条只没结解问意建月公无系军很情者最立代想已通并提直题党程展五果料象员革位入常文总次品式活设及管特件长求老头基资边流路级少图山统接知较将组见计别她手角期根论运农指几九区强放决", "我能吞下玻璃而不伤身体。今天天气很好，我们一起去吧！", latin=False),
    Language("zh-Hant", "Chinese (Traditional)", "的一是在不了有和人這中大為上個國我以要他時來用們生到作地於出就分對成會可主發年動同工也能下過子說產種面而方後多定行學法所民得經十三之進著等部度家電力裡如水化高自二理起小物現實加量都兩體制機當使點從業本去把性好應開它合還因由其些然前外天政四日那社義事平形相全表間樣與關各重新線內數正心反你明看原又麼利比或但質氣第向道命此變條只沒結解問意建月公無系軍很情者最立代想已通並提直題黨程展五果料象員革位入常文總次品式活設及管特件長求老頭基資邊流路級少圖山統接知較將組見計別她手角期根論運農指幾九區強放決", "我能吞下玻璃而不傷身體。今天天氣很好，我們一起去吧！", latin=False),
    Language("ja", "Japanese", "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをんがぎぐげござじずぜぞだでどばびぶべぼぱぴぷぺぽゃゅょっアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲンガギグゲゴザジズゼゾダデドバビブベボパピプペポャュョッー日本語漢字人大年一国会社時間今見行来出事上下中手自分電話病院警察銀行音楽劇場", "いろはにほへと ちりぬるを。私はガラスを食べられます。それは私を傷つけません。", latin=False),
    Language("ko", "Korean", "가나다라마바사아자차카타파하한글고노도로모보소오조초코토포호구누두루무부수우주추쿠투푸후그느드르므브스으즈츠크트프흐기니디리미비시이지치키티피히개내대래매배새애재채케테페해국민인일이사람시간오늘내일병원경찰은행음악극장전화생각", "다람쥐 헌 쳇바퀴에 타고파. 나는 유리를 먹을 수 있어요. 그래도 아프지 않아요.", latin=False),
    # Right-to-left, joined letters, and scripts whose letters stack: new text layout, not just letters.
    Language("ar", "Arabic", "ابتثجحخدذرزسشصضطظعغفقكلمنهوي", "صف خلق خود كمثل الشمس إذ بزغت.", latin=False),
    Language("fa", "Persian", "ابپتثجچحخدذرزژسشصضطظعغفقکگلمنوهی", "من می‌توانم بدون احساس درد شیشه بخورم.", latin=False),
    Language("ur", "Urdu", "ابپتٹثجچحخدڈذرڑزژسشصضطظعغفقکگلمنںوہھءیے", "میں کانچ کھا سکتا ہوں اور مجھے تکلیف نہیں ہوتی۔", latin=False),
    Language("he", "Hebrew", "אבגדהוזחטיכלמנסעפצקרשתךםןףץ", "דג סקרן שט בים מאוכזב ולפתע מצא חברה.", latin=False),
    Language("hi", "Hindi", "अआइईउऊएऐओऔकखगघचछजझटठडढणतथदधनपफबभमयरलवशषसह", "मैं काँच खा सकता हूँ और मुझे उससे कोई चोट नहीं पहुँचती।", latin=False),
    Language("bn", "Bengali", "অআইঈউঊএঐওঔকখগঘঙচছজঝঞটঠডঢণতথদধনপফবভমযরলশষসহ", "আমি কাঁচ খেতে পারি, তাতে আমার কোনো ক্ষতি হয় না।", latin=False),
    Language("th", "Thai", "กขคงจฉชซญดตถทธนบปผพฟภมยรลวศษสหอฮ", "ฉันกินกระจกได้ แต่มันไม่ทำให้ฉันเจ็บ", latin=False),
]

BY_CODE = {lang.code: lang for lang in LANGUAGES}
CJK = {"zh-Hans", "zh-Hant", "ja", "ko"}            # letters kept in the pack's own dictionary
NEEDS_LAYOUT = {"ar", "fa", "ur", "hi", "bn", "th"}   # letters alone won't do: see docs/LOCALIZATION.md


SIGNS = ["(…;)", "—–", "„“”", "‘’‚·"]     # no language needs them all; a test pack shows them


def full_alphabet(lang: Language) -> str:
    """Every letter of the language, both cases, plus a-z for the Latin ones."""
    letters = lang.letters + ("abcdefghijklmnopqrstuvwxyz" if lang.latin else "")
    capitals = "".join(c.upper() for c in letters if c not in lang.no_upper and len(c.upper()) == 1)
    return "".join(dict.fromkeys(letters + capitals + lang.upper))


def missing(lang: Language) -> str:
    """The letters of `lang` the pack tool can't write yet."""
    from . import cjk, gametext as gt, letters
    known = set(gt.CODE_OF) | set(gt._latin_codes()) | set(letters.CATALOG)
    return "".join(dict.fromkeys(c for c in full_alphabet(lang) + lang.sample
                                 if c not in known and c != " " and not cjk.supported(ord(c))))


def sample_words(lang: Language) -> list[str]:
    """What a test pack says: the language's own letters first, then its sample."""
    if lang.code in CJK:
        return [c for c in lang.sample + lang.letters if c != " "]
    special = [c for c in full_alphabet(lang) if not c.isascii()]
    return ["".join(special[i:i + 7]) for i in range(0, len(special), 7)] + lang.sample.split() + SIGNS
