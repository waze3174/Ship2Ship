"""Unit tests for the Ship2Ship apworld that need neither Archipelago nor comborando.

The world's own modules (tables, oracle, reach, seedout) never import Archipelago, so they are loaded here without
running apworld/ship2ship/__init__.py. The oracle is a fake: scripts/ap/fake_oracle.py over a four-check toy world.
Generation against the real Archipelago and comborando is the CI's job (scripts/ap/ap_generate.py).

  python -I scripts/ap/test_apworld.py
"""
import json
import pathlib
import re
import sys
import tempfile
import threading
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
WORLD = ROOT / "apworld" / "ship2ship"
SCRIPTS = pathlib.Path(__file__).resolve().parent
FAKE_ORACLE = SCRIPTS / "fake_oracle.py"
HEADLESS = ROOT / "combo" / "ComboRandoHeadless.cpp"
SHARED_ITEMS_HEADER = ROOT / "combo" / "rando" / "SharedItems.h"
CROSS_ENTRANCE_HEADER = ROOT / "combo" / "rando" / "CrossEntrance.h"
LOST_WOODS_HEADER = ROOT / "combo" / "rando" / "ComboLostWoodsLink.h"


sys.path.insert(0, str(SCRIPTS))
from world_modules import load_module, load_world_package  # noqa: E402

load_world_package()
from ship2ship import oracle, reach, seedout, tables  # noqa: E402

pack_apworld = load_module("pack_apworld", SCRIPTS / "pack_apworld.py")
update_tables = load_module("update_tables", SCRIPTS / "update_tables.py")

KNOWN = {"Key (OoT)": 1, "Mask (MM)": 2, "Blue Rupee (OoT)": 3, "Triforce Piece (OoT)": 4,
         "Piece of the Triforce (MM)": 5}


def fake_command(*extra):
    return [sys.executable, "-I", str(FAKE_ORACLE), "--oracle", *extra]


def started_fake(*extra):
    process = oracle.OracleProcess(fake_command(*extra), str(SCRIPTS))
    process.check_protocol()
    return process


def default_settings(**changes):
    values = dict(triforce_hunt=False, pieces_required=10, pieces_total=14, shared_items=frozenset(),
                  unified_inventory=False, form_logic_credit=False, lost_woods_link=True, mm_entrances=frozenset(),
                  cross_entrances=frozenset(), cross_entrance_percent=50)
    values.update(changes)
    return oracle.OpenSettings(**values)


class TablesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tables = tables.static_tables()

    def test_every_name_has_a_game_tag_and_an_id_in_its_games_range(self):
        for table, ids in (("items", self.tables.item_ids), ("locations", self.tables.location_ids)):
            for name, number in ids.items():
                game, _ = tables.split_ap_name(name)
                base = {"oot": 1_000_000, "mm": 2_000_000}[game]
                self.assertTrue(base < number < base + 1_000_000, f"{table}: {name} = {number}")
            self.assertEqual(len(set(ids.values())), len(ids), f"{table} reuses an id")

    def test_split_and_join_names(self):
        self.assertEqual(tables.split_ap_name("Progressive Hookshot (OoT)"), ("oot", "Progressive Hookshot"))
        self.assertEqual(tables.ap_name("mm", "Hookshot"), "Hookshot (MM)")
        with self.assertRaises(ValueError):
            tables.split_ap_name("Victory")

    def test_the_names_the_code_relies_on_exist(self):
        for name in tables.TRIFORCE_PIECE_NAMES:
            self.assertEqual(self.tables.item_facts[name].item_class, tables.CLASS_PROGRESSION, name)
        for name in tables.STATIC_FILLER_NAMES:
            self.assertEqual(self.tables.item_facts[name].item_class, tables.CLASS_FILLER, name)

    def test_item_class_follows_the_oracle_rule(self):
        self.assertEqual(tables.item_class_of("junk", False, True), tables.CLASS_TRAP)
        self.assertEqual(tables.item_class_of("major", True, False), tables.CLASS_PROGRESSION)
        self.assertEqual(tables.item_class_of("junk", False, False), tables.CLASS_FILLER)
        self.assertEqual(tables.item_class_of("mask", False, False), tables.CLASS_USEFUL)
        self.assertEqual(tables.strongest_class(tables.CLASS_FILLER, tables.CLASS_PROGRESSION), tables.CLASS_PROGRESSION)

    def test_groups_cover_every_name_and_never_collide_with_one(self):
        item_groups, location_groups = self.tables.item_groups(), self.tables.location_groups()
        self.assertEqual(set().union(*(item_groups[g] for g in ("OoT", "MM"))), set(self.tables.item_ids))
        self.assertEqual(set().union(*(location_groups[g] for g in ("OoT", "MM"))), set(self.tables.location_ids))
        self.assertFalse(set(item_groups) & set(self.tables.item_ids))
        self.assertFalse(set(location_groups) & set(self.tables.location_ids))
        self.assertIn("Small Keys", item_groups)
        self.assertEqual(tables.area_group_name("mm", "in Termina Field"), "MM: Termina Field")

    def test_shared_families_match_the_header_and_name_real_items(self):
        header = SHARED_ITEMS_HEADER.read_text(encoding="utf-8")
        start = header.index("inline const SharedFamilyDef* SharedFamilyTable()")
        rows = re.findall(r'\{\s*SF_\w+,\s*"(\w+)",.*?,\s*"([^"]*)",\s*"([^"]*)",\s*\d+,',
                          header[start:header.index("return table;", start)], re.S)
        self.assertEqual({key: (oot, mm or None) for key, oot, mm in rows},
                         {key: (f.oot_name, f.mm_name) for key, f in self.tables.shared_families.items()})
        for family in self.tables.shared_families.values():
            for name in family.ap_names():
                self.assertIn(name, self.tables.item_ids)

    def test_entrance_keys_match_the_header(self):
        header = CROSS_ENTRANCE_HEADER.read_text(encoding="utf-8")

        def keys_of(function_marker, struct_marker):
            body = header[header.index(struct_marker):]
            body = body[body.index(function_marker):]
            return re.findall(r'j\.value\("(\w+)"', body[:body.index("\n    }\n")])

        self.assertEqual(keys_of("static EntranceSettings FromJson", "struct EntranceSettings"),
                         list(tables.MM_ENTRANCE_KEYS))
        self.assertEqual([k for k in keys_of("static CrossSettings FromJson", "struct CrossSettings")
                          if k != "percent"], list(tables.CROSS_ENTRANCE_KEYS))

    def test_a_table_that_disagrees_with_its_groups_is_refused(self):
        files = {name: (WORLD / "data" / name).read_bytes() for name in ("ids.json", "groups.json",
                                                                          "shared_families.json")}
        ids = json.loads(files["ids.json"])
        ids["items"]["Extra Item (OoT)"] = 1_999_999
        files["ids.json"] = json.dumps(ids).encode()
        with self.assertRaises(ValueError):
            tables.load_tables(lambda name: files[name])


class UpdateTablesTest(unittest.TestCase):
    def test_groups_are_built_from_a_dump(self):
        dump = {"items": [{"apName": "A (OoT)", "category": "major", "advancement": True, "trap": False}],
                "locations": [{"apName": "L (MM)", "area": "in Somewhere"}]}
        self.assertEqual(update_tables.build_groups(dump),
                         {"version": 1, "items": {"A (OoT)": {"category": "major", "advancement": True, "trap": False}},
                          "locations": {"L (MM)": "in Somewhere"}})


class ReachCacheTest(unittest.TestCase):
    def make_cache(self, delay_event=None):
        self.asked = []

        def ask(request):
            self.asked.append(request)
            if delay_event:
                delay_event.wait(5)
            return {"mask": "5", "win": {"ganon": True, "majora": False, "pieces": 0, "won": False}}

        return reach.ReachCache(ask, KNOWN)

    def test_the_key_is_sorted_and_drops_unknown_items_zeroes_and_triforce_pieces(self):
        key = reach.reach_key({"Mask (MM)": 1, "Key (OoT)": 2, "Victory": 1, "Blue Rupee (OoT)": 0,
                               "Triforce Piece (OoT)": 3}, KNOWN)
        self.assertEqual(key, (("Key (OoT)", 2), ("Mask (MM)", 1)))

    def test_the_request_names_items_by_the_games_own_names_per_game(self):
        request = reach.request_for((("Key (OoT)", 2), ("Mask (MM)", 1)))
        self.assertEqual(request, {"cmd": "reach", "items": {"oot": {"Key": 2}, "mm": {"Mask": 1}}})

    def test_counting_pieces_covers_both_games(self):
        self.assertEqual(reach.count_pieces({"Triforce Piece (OoT)": 3, "Piece of the Triforce (MM)": 2, "Key (OoT)": 9}), 5)

    def test_the_same_items_ask_once_whatever_their_order_or_pieces(self):
        cache = self.make_cache()
        first = cache.answer({"Key (OoT)": 1, "Mask (MM)": 1})
        again = cache.answer({"Mask (MM)": 1, "Key (OoT)": 1, "Triforce Piece (OoT)": 4})
        self.assertIs(first, again)
        self.assertEqual(len(self.asked), 1)
        self.assertEqual(first.mask, 5)
        self.assertTrue(first.ganon)
        cache.answer({"Key (OoT)": 2})
        self.assertEqual((len(self.asked), cache.misses, len(cache)), (2, 2, 2))

    def test_threads_asking_for_the_same_items_share_one_question(self):
        release = threading.Event()
        cache = self.make_cache(release)
        results = []
        threads = [threading.Thread(target=lambda: results.append(cache.answer({"Key (OoT)": 1}))) for _ in range(12)]
        for thread in threads:
            thread.start()
        release.set()
        for thread in threads:
            thread.join(10)
        self.assertEqual(len(results), 12)
        self.assertEqual(len(self.asked), 1)

    def test_the_win_rules(self):
        both = oracle.ReachAnswer(1, True, True)
        ganon_only = oracle.ReachAnswer(1, True, False)
        self.assertTrue(oracle.is_won(oracle.WIN_BOSSES, both, 0, 0))
        self.assertFalse(oracle.is_won(oracle.WIN_BOSSES, ganon_only, 0, 0))
        self.assertTrue(oracle.is_won(oracle.WIN_GANON, ganon_only, 0, 0))
        self.assertFalse(oracle.is_won(oracle.WIN_MAJORA, ganon_only, 0, 0))
        self.assertFalse(oracle.is_won(oracle.WIN_PIECES, both, 9, 10))
        self.assertTrue(oracle.is_won(oracle.WIN_PIECES, ganon_only, 10, 10))


class OracleProcessTest(unittest.TestCase):
    def test_hello_open_reach_and_finalize_through_a_real_pipe(self):
        process = started_fake()
        self.addCleanup(process.close)
        self.assertEqual(process.library, {"ship2ship": "fake"})
        opened = oracle.parse_opened(process.call(default_settings().to_request(7, "label")))
        self.assertEqual(opened.locations[3], ("mm", "M0"))
        self.assertEqual(opened.win_condition, oracle.WIN_BOSSES)
        self.assertEqual([e.item_class for e in opened.pool], ["progression", "progression", "filler"])
        answer = oracle.parse_reach(process.call(reach.request_for((("Key (OoT)", 2), ("Mask (MM)", 1)))))
        self.assertEqual(answer.mask, 0b1111)
        self.assertTrue(answer.ganon and answer.majora)
        seed = process.call(seedout.finalize_request([seedout.own_placement("oot", "L0", "oot", "Key")]))["seed"]
        self.assertEqual(seed["placements"][0]["item"], {"game": "oot", "name": "Key"})

    def test_many_threads_never_get_each_others_answers(self):
        process = started_fake()
        self.addCleanup(process.close)
        failures = []

        def worker(keys):
            for _ in range(60):
                answer = oracle.parse_reach(process.call(reach.request_for((("Key (OoT)", keys),))))
                if answer.mask != [0b0001, 0b0011, 0b0111][keys]:
                    failures.append((keys, answer.mask))

        threads = [threading.Thread(target=worker, args=(n % 3,)) for n in range(9)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(30)
        self.assertEqual(failures, [])

    def test_a_refusal_carries_the_oracles_reason(self):
        process = started_fake()
        self.addCleanup(process.close)
        with self.assertRaisesRegex(oracle.OracleError, "asked to fail"):
            process.call({"cmd": "fail"})

    def test_an_oracle_that_dies_says_what_it_printed(self):
        process = started_fake("--die-on-reach")
        self.addCleanup(process.close)
        with self.assertRaisesRegex(oracle.OracleError, "exited during 'reach'.*dying on reach"):
            process.call(reach.request_for(()))

    def test_another_protocol_version_is_refused(self):
        process = oracle.OracleProcess(fake_command("--protocol", "2"), str(SCRIPTS))
        self.addCleanup(process.close)
        with self.assertRaisesRegex(oracle.OracleError, "protocol 1.*speaks 2"):
            process.check_protocol()

    def test_a_missing_executable_is_an_oracle_error(self):
        with self.assertRaisesRegex(oracle.OracleError, "could not start"):
            oracle.OracleProcess.start(str(SCRIPTS / "no-such-comborando"), str(SCRIPTS))


class OpenSettingsTest(unittest.TestCase):
    def test_the_request_has_the_shape_comborandos_open_reads(self):
        request = default_settings(triforce_hunt=True, shared_items=frozenset({"hookshot", "bows"}),
                                   mm_entrances=frozenset({"dungeon"}), cross_entrances=frozenset({"overworld"}),
                                   oot_settings={"gRandoSettings.ShuffleCows": True},
                                   oot_tricks=frozenset({"b", "a"})).to_request(5, "seed")
        self.assertEqual(request["goal"], {"type": "triforceHunt", "requiredPieces": 10, "totalPieces": 14})
        self.assertEqual(request["sharedItems"], ["bows", "hookshot"])
        self.assertEqual(request["startingGame"], "OOT")
        self.assertEqual(request["entranceSettings"]["mm"]["dungeon"], True)
        self.assertEqual(request["entranceSettings"]["mm"]["mixed"], False)
        self.assertEqual(request["entranceSettings"]["cross"], {"overworld": True, "interior": False,
                                                                 "dungeon": False, "percent": 50})
        self.assertEqual(request["settings"]["oot"], {"settings": {"gRandoSettings.ShuffleCows": 1},
                                                      "enabledTricks": ["a", "b"]})
        json.dumps(request)

    def test_every_key_it_sends_is_one_the_open_handler_reads(self):
        source = HEADLESS.read_text(encoding="utf-8")
        handler = source[source.index("auto openWorld"):source.index("auto finalizeWorld")]
        for header in (LOST_WOODS_HEADER, CROSS_ENTRANCE_HEADER):  # keys the handler reads through these
            handler += header.read_text(encoding="utf-8")
        request = default_settings().to_request(5, "seed")
        for key in (*(k for k in request if k != "cmd"), "mm", "cross", "oot", "enabledTricks", "percent"):
            self.assertTrue(f'"{key}"' in handler, f"openWorld does not read '{key}'")

    def test_settings_that_are_not_numbers_or_text_are_refused(self):
        with self.assertRaisesRegex(ValueError, "whole number or text"):
            default_settings(mm_settings={"gRando.X": 1.5}).to_request(1, "s")
        with self.assertRaisesRegex(ValueError, "not a setting name"):
            default_settings(oot_settings={3: 1}).to_request(1, "s")


class SeedOutputTest(unittest.TestCase):
    def test_a_seed_survives_the_slot_data_round_trip_and_shrinks(self):
        seed = {"games": "ootmm", "oot": {"placements": {f"Check {i}": f"Item {i % 40}" for i in range(6000)}}}
        text = seedout.encode_seed(seed)
        self.assertEqual(seedout.decode_seed(text), seed)
        self.assertLess(len(text), len(json.dumps(seed)) // 2)

    def test_slot_data_is_plain_json_with_the_encoding_named(self):
        data = seedout.slot_data_for({"a": 1}, {"startingGame": "OOT"}, {"ship2ship": "0.3.0"}, 99, 1)
        self.assertEqual(json.loads(json.dumps(data)), data)
        self.assertEqual((data["seed_encoding"], data["master_seed"], data["oracle_protocol"]), ("zlib+base64", 99, 1))
        self.assertEqual(seedout.decode_seed(data["seed"]), {"a": 1})

    def test_placement_shapes(self):
        self.assertEqual(seedout.other_placement("mm", "Check", "Slot 2", "Sword", True),
                         {"game": "mm", "check": "Check",
                          "other": {"player": "Slot 2", "name": "Sword", "advancement": True}})
        self.assertEqual(seedout.other_players_items({"archipelago": {"otherPlayersItems": [1]}}), [1])
        self.assertEqual(seedout.other_players_items({}), [])


class PackagingTest(unittest.TestCase):
    def test_the_apworld_has_one_folder_and_a_loadable_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            out = pack_apworld.pack(WORLD, pathlib.Path(folder) / "ship2ship.apworld")
            with zipfile.ZipFile(out) as archive:
                names = set(archive.namelist())
                manifest = json.loads(archive.read("ship2ship/archipelago.json"))
            self.assertTrue(all(name.startswith("ship2ship/") for name in names))
            for required in ("__init__.py", "data/ids.json", "data/groups.json", "data/shared_families.json",
                             "docs/setup_en.md", "docs/en_Ship2Ship.md"):
                self.assertIn(f"ship2ship/{required}", names)
            self.assertFalse([n for n in names if "__pycache__" in n])
            self.assertEqual((manifest["game"], manifest["compatible_version"], manifest["version"]),
                             ("Ship2Ship", 7, 7))
            self.assertRegex(manifest["minimum_ap_version"], r"^\d+\.\d+\.\d+$")


if __name__ == "__main__":
    unittest.main(verbosity=2)
