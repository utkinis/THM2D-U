#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
#define MF_BEGIN_DECL extern "C" {
#define MF_END_DECL }
#else
#define MF_BEGIN_DECL
#define MF_END_DECL
#endif

MF_BEGIN_DECL

// Return error codes
typedef enum mf_status {
  MF_OK,
  MF_ERROR_FAILED_IO_OPERATION,
  MF_ERROR_INVALID_FILE,
  MF_ERROR_INVALID_READ_REQUEST,
  MF_ERROR_MISSING_PROPERTY,
  MF_ERROR_UNSUPPORTED_FEATURE_REQUIRED
} mf_status_t;

// MUFITS files can be saved in either binary or text format; this lib currently
// supports only binary format
typedef enum mf_file_format { MF_BINARY, MF_ASCII } mf_file_format_t;

typedef enum mf_data_type {
  MF_INT1,
  MF_INT2,
  MF_INT4,
  MF_REAL4,
  MF_REAL8,
  MF_CHAR4,
  MF_CHAR8
} mf_data_type_t;

// In MUFITS file format, all properties have either one or two elements per
// object, e.g. graph connection ID consists of two IDs of connected cells
typedef enum mf_output_mode { MF_SINGLE, MF_DOUBLE } mf_output_mode_t;

// Some properties are defined not for the whole fluid, but per phase
typedef enum mf_phase_state { MF_STATE0, MF_STATE1 } mf_phase_state_t;

// When reading or writing MUFITS files, data layout may not be contiguous for
// each property, e.g. properties that are defined per phase usually are stored
// in interleaved manner
typedef struct mf_data {
  void *bytes;
  size_t stride;
  int32_t count;
} mf_data_t;

// SUM file handle
typedef struct mf_sum_file mf_sum_file_t;

typedef struct mf_time {
  double value;
  char dimension[9];
} mf_time_t;

typedef struct mf_date {
  int32_t day;
  char month[9];
  int32_t year;
} mf_date_t;

typedef struct mf_property {
  char name[9];
  char dimension[9];
  mf_data_type_t data_type;
  mf_output_mode_t output_mode;
  mf_phase_state_t phase_state;
} mf_property_t;

typedef struct mf_arrays {
  int32_t num_properties;
  int32_t num_objects;
  mf_property_t *properties;
} mf_arrays_t;

typedef struct mf_sum_description {
  mf_time_t *time;
  mf_date_t *date;
  mf_arrays_t *celldata;
  mf_arrays_t *conndata;
  mf_arrays_t *srcdata;
  mf_arrays_t *fpcedata;
  mf_arrays_t *fpcodata;
} mf_sum_description_t;

// Structure mf_sum_attachment_t stores properties data when reading and writing
// SUM files
typedef struct mf_sum_attachment {
  mf_data_t *celldata;
  mf_data_t *conndata;
  mf_data_t *srcdata;
  mf_data_t *fpcedata;
  mf_data_t *fpcodata;
} mf_sum_attachment_t;

// MVS file handle
typedef struct mf_mvs_file mf_mvs_file_t;

typedef struct mf_mvs_description {
  int32_t num_vertices;
  int32_t num_cells;
} mf_mvs_description_t;

// Structure mf_mvs_attachment stores grid points and cells
typedef struct mf_mvs_attachment {
  double (*points)[3];
  int32_t *cell_ids;
  int32_t (*cells)[8];
} mf_mvs_attachment_t;

// Read API

// Structure mf_sum_block_query_t is used to list properties that will be read
// from block
typedef struct mf_sum_block_query {
  char (*names)[9];
  int32_t num_items;
} mf_sum_block_query_t;

// Structure mf_sum_read_request_t contains batched block queries
typedef struct mf_sum_read_request {
  mf_sum_block_query_t *celldata;
  mf_sum_block_query_t *conndata;
  mf_sum_block_query_t *srcdata;
  mf_sum_block_query_t *fpcedata;
  mf_sum_block_query_t *fpcodata;
} mf_sum_read_request_t;

mf_status_t mf_open_sum_file(mf_sum_file_t **file, const char *filename);
void mf_close_sum_file(mf_sum_file_t *file);

mf_sum_description_t *mf_get_sum_description(const mf_sum_file_t *file);

mf_status_t mf_read_sum_file(mf_sum_file_t *file,
                             const mf_sum_read_request_t *request,
                             mf_sum_attachment_t *attachment);

mf_status_t mf_open_mvs_file(mf_mvs_file_t **file, const char *filename);
void mf_close_mvs_file(mf_mvs_file_t *file);

mf_mvs_description_t *mf_get_mvs_description(const mf_mvs_file_t *file);

mf_status_t mf_read_mvs_file(mf_mvs_file_t *file, mf_mvs_attachment_t *data);

// Write API

mf_status_t mf_write_sum_file(FILE *stream, const mf_sum_description_t *desc,
                              mf_sum_attachment_t *data);

mf_status_t mf_write_mvs_file(FILE *stream, const mf_mvs_description_t *desc,
                              mf_mvs_attachment_t *data);

MF_END_DECL
