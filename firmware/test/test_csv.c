// csv tests, the important one is that a nasty ssid cannot move the columns

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "csv.h"

static hg_record_t rec(const char *ssid, uint8_t type, uint8_t channel)
{
    hg_record_t r;
    memset(&r, 0, sizeof r);

    uint8_t mac[6] = { 0xaa, 0xbb, 0xcc, 0x11, 0x22, 0x33 };
    memcpy(r.bssid, mac, sizeof mac);

    r.rssi    = -55;
    r.channel = channel;
    r.type    = type;
    snprintf(r.ssid, sizeof r.ssid, "%s", ssid);

    hg_record_seal(&r);
    return r;
}

// walks a row the way a csv reader would
// a comma or a newline inside quotes is data, only a bare one counts
static int count_fields(const char *row)
{
    int fields = 1;
    int inside = 0;

    for (const char *p = row; *p != '\0'; p++) {
        if (*p == '"')
            inside = !inside;
        else if (*p == ',' && !inside)
            fields++;
        else if (*p == '\n' && !inside)
            break;
    }
    return fields;
}

static void plain_ssid_is_left_alone(void)
{
    char buf[64];
    csv_field(buf, sizeof buf, "HomeWiFi", 8);
    assert(strcmp(buf, "HomeWiFi") == 0);
}

static void empty_ssid_stays_empty(void)
{
    char buf[64];
    assert(csv_field(buf, sizeof buf, "", 0) == 0);
    assert(strcmp(buf, "") == 0);
}

static void comma_gets_quoted(void)
{
    char buf[64];
    csv_field(buf, sizeof buf, "cafe, downtown", 14);
    assert(strcmp(buf, "\"cafe, downtown\"") == 0);
}

// a quote has to come out doubled or every reader downstream desyncs
static void quote_is_doubled(void)
{
    char buf[64];
    csv_field(buf, sizeof buf, "the \"good\" wifi", 15);
    assert(strcmp(buf, "\"the \"\"good\"\" wifi\"") == 0);
}

static void newline_gets_quoted(void)
{
    char buf[64];
    csv_field(buf, sizeof buf, "line\nbreak", 10);
    assert(strcmp(buf, "\"line\nbreak\"") == 0);

    csv_field(buf, sizeof buf, "carriage\rreturn", 15);
    assert(strcmp(buf, "\"carriage\rreturn\"") == 0);
}

// verify the CSV export contract
static void hostile_ssids_keep_the_column_count(void)
{
    const char *nasty[] = {
        "normal",
        "",
        "has,commas,everywhere",
        "has \"quotes\" in it",
        "line\nbreak",
        "\"",
        ",,,,,,,,,,",
        "mix,of \"both\"\nkinds",
    };

    char row[512];

    for (size_t i = 0; i < sizeof nasty / sizeof nasty[0]; i++) {
        hg_record_t r = rec(nasty[i], HG_TYPE_AP, 6);
        csv_row(row, sizeof row, &r, NULL);
        assert(count_fields(row) == 14);
    }
}

static void header_has_matching_column_count(void)
{
    char buf[512];
    csv_header(buf, sizeof buf);

    // second line is the column list
    const char *cols = strchr(buf, '\n');
    assert(cols != NULL);
    assert(count_fields(cols + 1) == 14);
}

static void ble_row_says_ble(void)
{
    char row[256];
    hg_record_t r = rec("", HG_TYPE_BLE, 0);

    csv_row(row, sizeof row, &r, NULL);
    assert(strstr(row, ",BLE\n") != NULL);
    assert(count_fields(row) == 14);
}

static void no_fix_writes_zeros(void)
{
    char row[256];
    hg_record_t r = rec("somewhere", HG_TYPE_AP, 1);

    csv_row(row, sizeof row, &r, NULL);
    assert(strstr(row, "0.000000,0.000000") != NULL);
}

static void fix_lands_in_the_row(void)
{
    char row[256];
    hg_record_t r = rec("somewhere", HG_TYPE_AP, 1);

    // Synthetic location/time fixture, not a recorded field observation.
    csv_fix fix = {
        .lat = 40.7128, .lon = -74.006,
        .alt_m = 12.5, .accuracy_m = 3.0,
        .first_seen = "2026-08-04 10:00:00",
        .have_fix = 1,
    };

    csv_row(row, sizeof row, &r, &fix);
    assert(strstr(row, "40.712800,-74.006000") != NULL);
    assert(strstr(row, "2026-08-04 10:00:00") != NULL);
    assert(count_fields(row) == 14);
}

// a short buffer must not run off the end and must still report the real size
static void truncation_is_safe(void)
{
    hg_record_t r = rec("a long enough name to overflow", HG_TYPE_AP, 11);

    int needed = csv_row(NULL, 0, &r, NULL);
    assert(needed > 0);

    char small[16];
    memset(small, 'x', sizeof small);

    int again = csv_row(small, sizeof small, &r, NULL);
    assert(again == needed);
    assert(small[sizeof small - 1] == '\0');
}

// 32 byte ssid is the longest the record holds, must not lose the last byte
static void full_length_ssid_survives(void)
{
    char name[33];
    memset(name, 'A', 32);
    name[32] = '\0';

    hg_record_t r = rec(name, HG_TYPE_AP, 1);
    char row[256];

    csv_row(row, sizeof row, &r, NULL);
    assert(strstr(row, name) != NULL);
}

static void stray_bytes_become_escapes(void)
{
    char buf[64];

    // Synthetic invalid UTF-8 byte: legal SSID data, escaped for text export.
    csv_field(buf, sizeof buf, "Net\xffwork", 8);
    assert(strcmp(buf, "Net\\xffwork") == 0);

    // a lone continuation byte, and a three byte sequence cut short
    csv_field(buf, sizeof buf, "\x80", 1);
    assert(strcmp(buf, "\\x80") == 0);

    csv_field(buf, sizeof buf, "\xe2\x82", 2);
    assert(strcmp(buf, "\\xe2\\x82") == 0);
}

static void real_utf8_is_left_alone(void)
{
    char buf[64];

    // an accented e, then a euro sign. both are valid and must survive
    csv_field(buf, sizeof buf, "caf\xc3\xa9", 5);
    assert(strcmp(buf, "caf\xc3\xa9") == 0);

    csv_field(buf, sizeof buf, "\xe2\x82\xac" "5", 4);
    assert(strcmp(buf, "\xe2\x82\xac" "5") == 0);
}

int main(void)
{
    plain_ssid_is_left_alone();
    empty_ssid_stays_empty();
    comma_gets_quoted();
    quote_is_doubled();
    stray_bytes_become_escapes();
    real_utf8_is_left_alone();
    newline_gets_quoted();
    hostile_ssids_keep_the_column_count();
    header_has_matching_column_count();
    ble_row_says_ble();
    no_fix_writes_zeros();
    fix_lands_in_the_row();
    truncation_is_safe();
    full_length_ssid_survives();

    printf("csv tests ok\n");
    return 0;
}
