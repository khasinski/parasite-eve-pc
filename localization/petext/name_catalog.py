"""The shared list of name letters (petext/names.py): letter k of this list is name letter number k
(from 1, skipping names.RESERVED) in every pack and every version of a pack, so a name saved with
one pack reads the same with any other pack that has its letters.

APPEND ONLY. A letter that is here keeps its place forever; new letters go at the end. Moving or
removing one changes the names in players' saves. The list may grow to the per-pack numbers
(names.OWN_FLOOR); test_names pins what is here today. The game's own letters (codes 00-4C) are
never here: names keep them as one byte.
"""

CATALOG = (
    # Latin-1 Supplement (62)
    "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞßà"
    "áâãäåæçèéêëìíîïðñòóôõöøùúûüýþÿ"
    # Latin Extended-A (128)
    "ĀāĂăĄąĆćĈĉĊċČčĎďĐđĒēĔĕĖėĘęĚěĜĝĞğ"
    "ĠġĢģĤĥĦħĨĩĪīĬĭĮįİıĲĳĴĵĶķĸĹĺĻļĽľĿ"
    "ŀŁłŃńŅņŇňŉŊŋŌōŎŏŐőŒœŔŕŖŗŘřŚśŜŝŞş"
    "ŠšŢţŤťŦŧŨũŪūŬŭŮůŰűŲųŴŵŶŷŸŹźŻżŽžſ"
    # Latin Extended-B: Azerbaijani, Vietnamese, pinyin, Romanian (26)
    "ƏəƠơƯưǍǎǏǐǑǒǓǔǕǖǗǘǙǚǛǜȘșȚț"
    # Vietnamese (90)
    "ẠạẢảẤấẦầẨẩẪẫẬậẮắẰằẲẳẴẵẶặẸẹẺẻẼẽẾế"
    "ỀềỂểỄễỆệỈỉỊịỌọỎỏỐốỒồỔổỖỗỘộỚớỜờỞở"
    "ỠỡỢợỤụỦủỨứỪừỬửỮữỰựỲỳỴỵỶỷỸỹ"
    # Greek (monotonic) (69)
    "ΆΈΉΊΌΎΏΐΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩ"
    "ΪΫάέήίΰαβγδεζηθικλμνξοπρςστυφχψω"
    "ϊϋόύώ"
    # Cyrillic, then Ukrainian and Kazakh (114)
    "ЀЁЂЃЄЅІЇЈЉЊЋЌЍЎЏАБВГДЕЖЗИЙКЛМНОП"
    "РСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмноп"
    "рстуфхцчшщъыьэюяѐёђѓєѕіїјљњћќѝўџ"
    "ҐґҒғҚқҢңҮүҰұҺһӘәӨө"
    # Hiragana (86)
    "ぁあぃいぅうぇえぉおかがきぎくぐけげこごさざしじすずせぜそぞただ"
    "ちぢっつづてでとどなにぬねのはばぱひびぴふぶぷへべぺほぼぽまみむ"
    "めもゃやゅゆょよらりるれろゎわゐゑをんゔゕゖ"
    # Katakana (91)
    "ァアィイゥウェエォオカガキギクグケゲコゴサザシジスズセゼソゾタダ"
    "チヂッツヅテデトドナニヌネノハバパヒビピフブプヘベペホボポマミム"
    "メモャヤュユョヨラリルレロヮワヰヱヲンヴヵヶヷヸヹヺー"
    # Hebrew, end-of-word forms in their Unicode places (27)
    "אבגדהוזחטיךכלםמןנסעףפץצקרשת"
)
