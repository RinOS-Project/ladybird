#ifndef LAGOM_RIN_ICU_HOST_STUB_H
#define LAGOM_RIN_ICU_HOST_STUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rin_icu_client {
    int fd;
    uint32_t next_request_id;
    uint32_t reserved[1];
} rin_icu_client_t;

typedef uint32_t rin_icu_handle_t;

typedef struct rin_icu_collator_options {
    uint32_t strength;
    uint32_t case_first;
    uint32_t numeric;
    uint32_t ignore_punctuation;
} rin_icu_collator_options_t;

int rin_icu_client_open(rin_icu_client_t* out_client);
void rin_icu_client_close(rin_icu_client_t* client);

int rin_icu_locale_canonicalize(rin_icu_client_t* client, char const* input, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_locale_resolve(rin_icu_client_t* client, char const* input, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_locale_maximize(rin_icu_client_t* client, char const* input, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_locale_minimize(rin_icu_client_t* client, char const* input, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_locale_available(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_locale_preferred(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len);

int rin_icu_normalize(rin_icu_client_t* client, char const* form, char const* input, char* dest, size_t dest_cap, size_t* out_len);

int rin_icu_collator_create(rin_icu_client_t* client, char const* locale, rin_icu_collator_options_t const* options, rin_icu_handle_t* out_handle);
int rin_icu_collator_compare(rin_icu_client_t* client, rin_icu_handle_t handle, char const* lhs, char const* rhs, int32_t* out_result);
int rin_icu_collator_destroy(rin_icu_client_t* client, rin_icu_handle_t handle);

int rin_icu_segmenter_create(rin_icu_client_t* client, char const* locale, uint32_t kind, rin_icu_handle_t* out_handle);
int rin_icu_segmenter_reset(rin_icu_client_t* client, rin_icu_handle_t handle, char const* input, size_t input_len);
int rin_icu_segmenter_next(rin_icu_client_t* client, rin_icu_handle_t handle, int32_t* out_position);
int rin_icu_segmenter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle);

int rin_icu_number_formatter_create(rin_icu_client_t* client, char const* locale, uint32_t style, rin_icu_handle_t* out_handle);
int rin_icu_number_formatter_format(rin_icu_client_t* client, rin_icu_handle_t handle, char const* value, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_number_formatter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle);

int rin_icu_datetime_formatter_create(rin_icu_client_t* client, char const* locale, uint32_t kind, int32_t tz_offset_minutes, uint32_t hour_cycle, rin_icu_handle_t* out_handle);
int rin_icu_datetime_formatter_format_epoch_ms(rin_icu_client_t* client, rin_icu_handle_t handle, double epoch_ms, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_datetime_formatter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle);

int rin_icu_plural_rules_create(rin_icu_client_t* client, char const* locale, uint32_t kind, rin_icu_handle_t* out_handle);
int rin_icu_plural_rules_select(rin_icu_client_t* client, rin_icu_handle_t handle, char const* value, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_plural_rules_destroy(rin_icu_client_t* client, rin_icu_handle_t handle);

int rin_icu_display_name(rin_icu_client_t* client, char const* locale, uint32_t type, char const* code, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_list_format(rin_icu_client_t* client, char const* locale, uint32_t type, char const* items, size_t items_len, uint32_t item_count, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_relative_time_format(rin_icu_client_t* client, char const* locale, char const* unit, char const* value, char* dest, size_t dest_cap, size_t* out_len);

int rin_icu_time_zone_current(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_time_zone_canonicalize(rin_icu_client_t* client, char const* time_zone, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_time_zone_available(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len);
int rin_icu_time_zone_offset(rin_icu_client_t* client, char const* time_zone, double epoch_ms, int32_t* out_offset_ms, int32_t* out_in_dst);

#ifdef __cplusplus
}
#endif

#endif
