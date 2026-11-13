"""The Steam Workshop screen's SERVER LISTS: the community server's text
(api-opence's /workshop/lists?format=text) read by steam_downloads.c."""
from pathlib import Path
import re
import subprocess
from mcc_test_support import compile_tool

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "port/linux/src/steam_downloads.c"


def block(source, start_pattern):
    """the text from start_pattern's match to the end of its braces (and a struct's ';')"""
    start = re.search(start_pattern, source).start()
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + (";" if source[end:end + 1] == ";" else "")


def test_lists_text_is_read(tmp_path):
    source = SOURCE.read_text()
    driver = "\n".join([
        "#include <assert.h>", "#include <stdint.h>", "#include <stdlib.h>", "#include <string.h>",
        "#include <stdio.h>", "enum { STEAM_URL_SIZE = 256 };",
        block(source, r"struct server_list\n\{"), block(source, r"struct list_item\n\{"),
        block(source, r"static void field_copy\("),
        block(source, r"int steam_downloads_parse_lists\("),
        r'''
int main(void) {
    static struct server_list lists[2];
    static struct list_item items[3];
    char text[] =
        "OPENCE-WORKSHOP-LISTS 1\r\n"
        "L\tguven/playtest\tGuven's Server\tMCC Workshop Playtest\tWhat we play\t2\n"
        "I\t2906110850\t0\t1740782978\t7508\tAerial\thttps://images/a.jpg\n"
        "I\t0\t0\t0\t0\tno id\t\n"
        "I\t3021510694\t1048576\t1\t2\tMercury Rising\thttp://not-https\n"
        "L\tother/one\tOther\tOne\t\t2\n"
        "I\t1\t0\t0\t0\tFirst\t\n"
        "I\t2\t0\t0\t0\tPast the most items\t\n"
        "L\tthird/x\tThird\tPast the most lists\t\t1\n"
        "I\t3\t0\t0\t0\tNot taken\t\n";
    char bad[] = "<html>";
    int count, item_count;
    count = steam_downloads_parse_lists(text, lists, 2, items, 3, &item_count);
    assert(count == 2 && item_count == 3);
    assert(!strcmp(lists[0].id, "guven/playtest") && !strcmp(lists[0].server, "Guven's Server"));
    assert(!strcmp(lists[0].name, "MCC Workshop Playtest") && !strcmp(lists[0].description, "What we play"));
    assert(lists[0].first == 0 && lists[0].count == 2);
    assert(items[0].id == 2906110850ull && items[0].subscriptions == 7508 && !strcmp(items[0].title, "Aerial"));
    assert(!strcmp(items[0].preview_url, "https://images/a.jpg"));
    assert(items[1].id == 3021510694ull && items[1].size == 1048576 && !items[1].preview_url[0]);
    assert(lists[1].first == 2 && lists[1].count == 1 && items[2].id == 1);
    assert(steam_downloads_parse_lists(bad, lists, 2, items, 3, &item_count) == -1 && !item_count);
    return 0;
}
'''])
    path = tmp_path / "lists.c"
    path.write_text(driver)
    exe = compile_tool(tmp_path, [path])
    subprocess.run([str(exe)], check=True)
