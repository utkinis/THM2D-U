#include "mufitsio.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define check_state(stream)                                                                                            \
  do {                                                                                                                 \
    if (ferror(stream)) {                                                                                              \
      return MF_ERROR_FAILED_IO_OPERATION;                                                                             \
    } else if (feof(stream)) {                                                                                         \
      return MF_ERROR_INVALID_FILE;                                                                                    \
    }                                                                                                                  \
  } while (0)

#define checked(code, err)                                                                                             \
  do {                                                                                                                 \
    err = code;                                                                                                        \
    if (err != MF_OK) {                                                                                                \
      fprintf(stderr, "Error occurred in file '%s' on line %d\n", __FILE__, __LINE__);                                 \
      goto on_error;                                                                                                   \
    }                                                                                                                  \
  } while (0)

#define fill_array(arr, size, val)                                                                                     \
  do {                                                                                                                 \
    for (int32_t tmp_idx__ = 0; tmp_idx__ < (size); ++tmp_idx__) {                                                     \
      (arr)[tmp_idx__] = (val);                                                                                        \
    }                                                                                                                  \
  } while (0)

typedef struct mf_sum_file {
  mf_file_format_t format;
  FILE *stream;
  int64_t celldata_offset;
  int64_t celldata_size;
  int64_t conndata_offset;
  int64_t conndata_size;
  int64_t srcdata_offset;
  int64_t srcdata_size;
  int64_t fpcedata_offset;
  int64_t fpcedata_size;
  int64_t fpcodata_offset;
  int64_t fpcodata_size;
  mf_sum_description_t *description;
} mf_sum_file_t;

typedef struct mf_mvs_file {
  mf_file_format_t format;
  FILE *stream;
  size_t vertices_offset;
  size_t cells_offset;
  mf_mvs_description_t *description;
} mf_mvs_file_t;

typedef struct {
  char name[9];
  int64_t size;
} header_t;

static mf_status_t read_header(header_t *h, FILE *stream);
static mf_status_t read_file_format(mf_file_format_t *format, FILE *stream);
static mf_status_t read_time(mf_time_t *t, FILE *stream);
static mf_status_t read_date(mf_date_t *d, FILE *stream);
static mf_status_t read_arrays(mf_arrays_t *arrays, int64_t *offset, int64_t *size, FILE *stream);
static mf_status_t read_data(FILE *stream, const mf_arrays_t *desc, const mf_sum_block_query_t *query, mf_data_t *data,
                             int64_t offset);
static void free_sum_description(mf_sum_description_t *desc);
static void write_block(FILE *stream, const char *name, const mf_arrays_t *arr, mf_data_t *data);

mf_status_t mf_open_sum_file(mf_sum_file_t **file, const char *filename) {
  assert(file);
  assert(filename);

  FILE *stream = fopen(filename, "rb");
  if (!stream) {
    fprintf(stderr, "Error: failed to open file '%s'\n", filename);
    perror("System error");
    return MF_ERROR_FAILED_IO_OPERATION;
  }

  mf_sum_file_t sum_file;
  sum_file.stream = stream;

  mf_status_t err = read_file_format(&sum_file.format, stream);
  if (err != MF_OK) {
    fprintf(stderr, "Error: failed to read file format\n");
    fclose(stream);
    return err;
  }

  if (sum_file.format != MF_BINARY) {
    fprintf(stderr, "Error: only binary file format is currently supported\n");
    fclose(stream);
    return MF_ERROR_UNSUPPORTED_FEATURE_REQUIRED;
  }

  mf_sum_description_t *desc = calloc(1, sizeof(mf_sum_description_t));

  while (true) {
    if (ferror(stream)) {
      fprintf(stderr, "Error: failed to perform read operation\n");
      perror("System error");
      err = MF_ERROR_FAILED_IO_OPERATION;
      goto on_error;
    }
    if (feof(stream)) {
      fprintf(stderr, "Error: unexpected end of file\n");
      err = MF_ERROR_INVALID_FILE;
      goto on_error;
    }

    header_t header;
    checked(read_header(&header, stream), err);

    if (!memcmp(header.name, "TIME    ", 8)) {
      desc->time = malloc(sizeof(mf_time_t));
      checked(read_time(desc->time, stream), err);
    } else if (!memcmp(header.name, "DATE    ", 8)) {
      desc->date = malloc(sizeof(mf_date_t));
      checked(read_date(desc->date, stream), err);
    } else if (!memcmp(header.name, "CELLDATA", 8)) {
      desc->celldata = malloc(sizeof(mf_arrays_t));
      checked(read_arrays(desc->celldata, &sum_file.celldata_offset, &sum_file.celldata_size, stream), err);
    } else if (!memcmp(header.name, "CONNDATA", 8)) {
      desc->conndata = malloc(sizeof(mf_arrays_t));
      checked(read_arrays(desc->conndata, &sum_file.conndata_offset, &sum_file.conndata_size, stream), err);
    } else if (!memcmp(header.name, "SRCDATA ", 8)) {
      desc->srcdata = malloc(sizeof(mf_arrays_t));
      checked(read_arrays(desc->srcdata, &sum_file.srcdata_offset, &sum_file.srcdata_size, stream), err);
    } else if (!memcmp(header.name, "FPCEDATA", 8)) {
      desc->fpcedata = malloc(sizeof(mf_arrays_t));
      checked(read_arrays(desc->fpcedata, &sum_file.fpcedata_offset, &sum_file.fpcedata_size, stream), err);
    } else if (!memcmp(header.name, "FPCODATA", 8)) {
      desc->fpcodata = malloc(sizeof(mf_arrays_t));
      checked(read_arrays(desc->fpcodata, &sum_file.fpcodata_offset, &sum_file.fpcodata_size, stream), err);
    } else if (!strcmp(header.name, "ENDFILE ")) {
      break;
    } else {
#ifdef _DEBUG
      fprintf(stderr, "Warning: unknown keyword '%s' (%lld bytes), skipping\n", header.name, header.size);
#endif
      fseek(sum_file.stream, (long)header.size, SEEK_CUR);
    }
  }

  sum_file.description = desc;
  *file = malloc(sizeof(mf_sum_file_t));
  memcpy(*file, &sum_file, sizeof(mf_sum_file_t));
  return MF_OK;

on_error:
  free_sum_description(desc);
  fclose(stream);
  return err;
}

void mf_close_sum_file(mf_sum_file_t *file) {
  fclose(file->stream);
  if (file->description) {
    free_sum_description(file->description);
  }
  free(file);
}

mf_sum_description_t *mf_get_sum_description(const mf_sum_file_t *file) {
  assert(file);
  return file->description;
}

mf_status_t mf_read_sum_file(mf_sum_file_t *file, const mf_sum_read_request_t *request,
                             mf_sum_attachment_t *attachment) {
  const mf_sum_description_t *desc = file->description;
  FILE *h = file->stream;
  mf_status_t err;
  if (request->celldata) {
    err = read_data(h, desc->celldata, request->celldata, attachment->celldata, file->celldata_offset);
    if (err != MF_OK) {
      return err;
    }
  }
  if (request->conndata) {
    err = read_data(h, desc->conndata, request->conndata, attachment->conndata, file->conndata_offset);
    if (err != MF_OK) {
      return err;
    }
  }
  if (request->srcdata) {
    err = read_data(h, desc->srcdata, request->srcdata, attachment->srcdata, file->srcdata_offset);
    if (err != MF_OK) {
      return err;
    }
  }
  if (request->fpcedata) {
    err = read_data(h, desc->fpcedata, request->fpcedata, attachment->fpcedata, file->fpcedata_offset);
    if (err != MF_OK) {
      return err;
    }
  }
  if (request->fpcodata) {
    err = read_data(h, desc->fpcodata, request->fpcodata, attachment->fpcodata, file->fpcodata_offset);
    if (err != MF_OK) {
      return err;
    }
  }
  return MF_OK;
}

mf_status_t mf_open_mvs_file(mf_mvs_file_t **file, const char *filename) {
  assert(file);

  FILE *stream = fopen(filename, "rb");
  if (!stream) {
    fprintf(stderr, "Error: failed to open file '%s'\n", filename);
    perror("System error");
    return MF_ERROR_FAILED_IO_OPERATION;
  }

  mf_mvs_file_t mvs_file;
  mvs_file.stream = stream;

  mf_status_t err = read_file_format(&mvs_file.format, stream);
  if (err != MF_OK) {
    fprintf(stderr, "Error: failed to read file format\n");
    fclose(stream);
    return err;
  }

  if (mvs_file.format != MF_BINARY) {
    fprintf(stderr, "Error: only binary file format is currently supported\n");
    fclose(stream);
    return MF_ERROR_UNSUPPORTED_FEATURE_REQUIRED;
  }

  mf_mvs_description_t *desc = calloc(1, sizeof(mf_mvs_description_t));

  header_t header;
  checked(read_header(&header, stream), err);

  if (memcmp(header.name, "GRIDDATA", 8)) {
    fprintf(stderr, "Error: expected 'GRIDDATA' block, got '%s'\n", header.name);
    err = MF_ERROR_INVALID_FILE;
    goto on_error;
  }

  checked(read_header(&header, stream), err);
  if (memcmp(header.name, "GRIDSIZE", 8)) {
    fprintf(stderr, "Error: expected 'GRIDSIZE' record, got '%s'\n", header.name);
    err = MF_ERROR_INVALID_FILE;
    goto on_error;
  }

  fread(&(desc->num_vertices), sizeof(int32_t), 1, stream);
  fread(&(desc->num_cells), sizeof(int32_t), 1, stream);

  assert(desc->num_vertices >= 0);
  assert(desc->num_cells >= 0);

  mvs_file.description = desc;

  checked(read_header(&header, stream), err);
  if (memcmp(header.name, "POINTS  ", 8)) {
    fprintf(stderr, "Error: expected 'POINTS' record, got '%s'\n", header.name);
    err = MF_ERROR_INVALID_FILE;
    goto on_error;
  }

  mvs_file.vertices_offset = ftell(stream);
  fseek(stream, (long)header.size, SEEK_CUR);

  checked(read_header(&header, stream), err);
  if (memcmp(header.name, "CELLS   ", 8)) {
    fprintf(stderr, "Error: expected 'CELLS' record, got '%s'\n", header.name);
    err = MF_ERROR_INVALID_FILE;
    goto on_error;
  }

  mvs_file.cells_offset = ftell(stream);

  (*file) = malloc(sizeof(mf_mvs_file_t));
  memcpy(*file, &mvs_file, sizeof(mf_mvs_file_t));

  return MF_OK;

on_error:
  free(desc);
  return err;
}

void mf_close_mvs_file(mf_mvs_file_t *file) {
  assert(file);
  fclose(file->stream);
  free(file->description);
  free(file);
}

mf_mvs_description_t *mf_get_mvs_description(const mf_mvs_file_t *file) {
  assert(file);
  return file->description;
}

mf_status_t mf_read_mvs_file(mf_mvs_file_t *file, mf_mvs_attachment_t *data) {
  assert(file);
  assert(data);

  const mf_mvs_description_t *desc = file->description;

  fseek(file->stream, (long)file->vertices_offset, SEEK_SET);
  for (int32_t vert_idx = 0; vert_idx < desc->num_vertices; ++vert_idx) {
    fread(data->points + vert_idx, 3 * sizeof(double), 1, file->stream);
  }

  fseek(file->stream, (long)file->cells_offset, SEEK_SET);
  for (int32_t cell_idx = 0; cell_idx < desc->num_cells; ++cell_idx) {
    fread(data->cell_ids + cell_idx, sizeof(int32_t), 1, file->stream);
    fread(data->cells + cell_idx, 8 * sizeof(int32_t), 1, file->stream);
  }

  return MF_OK;
}

mf_status_t mf_write_sum_file(FILE *stream, const mf_sum_description_t *desc, mf_sum_attachment_t *data) {
  assert(stream);
  assert(desc);
  assert(data);

  int64_t record_size;

  fwrite("BINARY  ", 1, 8, stream);
  record_size = 0;
  fwrite(&record_size, 8, 1, stream);

  if (desc->celldata) {
    write_block(stream, "CELLDATA", desc->celldata, data->celldata);
  }

  if (desc->conndata) {
    write_block(stream, "CONNDATA", desc->conndata, data->conndata);
  }

  if (desc->fpcedata) {
    write_block(stream, "FPCEDATA", desc->fpcedata, data->fpcedata);
  }

  if (desc->fpcodata) {
    write_block(stream, "FPCODATA", desc->fpcodata, data->fpcodata);
  }

  if (desc->srcdata) {
    write_block(stream, "SRCDATA ", desc->srcdata, data->srcdata);
  }

  fwrite("ENDFILE ", 1, 8, stream);
  record_size = 0;
  fwrite(&record_size, 8, 1, stream);

  return MF_OK;
}

mf_status_t mf_write_mvs_file(FILE *stream, const mf_mvs_description_t *desc, mf_mvs_attachment_t *data) {
  printf("Unsupported\n");
  stream = NULL;
  desc = NULL;
  data = NULL;
  return MF_ERROR_UNSUPPORTED_FEATURE_REQUIRED;
}

mf_status_t read_header(header_t *h, FILE *stream) {
  char buf[16];
  fread(buf, 16, 1, stream);
  check_state(stream);
  memcpy(h->name, buf, 8);
  h->name[8] = '\0';
  memcpy(&h->size, buf + 8, 8);
  return MF_OK;
}

mf_status_t read_file_format(mf_file_format_t *format, FILE *stream) {
  header_t header;
  mf_status_t err = read_header(&header, stream);
  if (err != MF_OK) {
    return err;
  }
  if (header.size != 0) {
    return MF_ERROR_INVALID_FILE;
  }
  if (!memcmp(header.name, "BINARY  ", 8)) {
    *format = MF_BINARY;
  } else if (!memcmp(header.name, "ASCII   ", 8)) {
    *format = MF_ASCII;
  } else {
    fprintf(stderr, "Error: unrecognized file format '%s'\n", header.name);
    return MF_ERROR_INVALID_FILE;
  }
  return MF_OK;
}

mf_status_t read_time(mf_time_t *t, FILE *stream) {
  char buf[16];
  fread(buf, 16, 1, stream);
  check_state(stream);
  memcpy(&t->value, buf, 8);
  memcpy(t->dimension, buf + 8, 8);
  t->dimension[8] = '\0';
  return MF_OK;
}

mf_status_t read_date(mf_date_t *d, FILE *stream) {
  char buf[16];
  fread(buf, 16, 1, stream);
  check_state(stream);
  memcpy(&d->day, buf, 4);
  memcpy(d->month, buf + 4, 8);
  d->month[8] = '\0';
  memcpy(&d->year, buf + 12, 4);
  return MF_OK;
}

mf_status_t read_arrays(mf_arrays_t *arrays, int64_t *offset, int64_t *size, FILE *stream) {
  header_t header;
  mf_status_t err = MF_OK;
  checked(read_header(&header, stream), err);

  assert(!memcmp(header.name, "ARRAYS  ", 8));
  char *buf = malloc(header.size);
  assert(buf);

  fread(buf, header.size, 1, stream);
  if (ferror(stream)) {
    err = MF_ERROR_FAILED_IO_OPERATION;
    goto on_buf_error;
  }
  if (feof(stream)) {
    err = MF_ERROR_INVALID_FILE;
    goto on_buf_error;
  }

  memcpy(&arrays->num_properties, buf, 4);
  memcpy(&arrays->num_objects, buf + 4, 4);

  assert(arrays->num_properties >= 0);
  assert(arrays->num_objects >= 0);

  if (arrays->num_properties == 0) {
    free(buf);
    arrays->properties = NULL;
    return MF_OK;
  }

  arrays->properties = calloc(arrays->num_properties, sizeof(mf_property_t));
  assert(arrays->properties);

  char *prop_buf = buf + 8;
  for (int32_t i = 0; i < arrays->num_properties; ++i) {
    mf_property_t *prop = arrays->properties + i;
    memcpy(&prop->name, prop_buf, 8);
    prop->name[8] = '\0';

    memcpy(prop->dimension, prop_buf + 8, 8);

    char tag[9];
    tag[8] = '\0';

    // Defaults
    prop->data_type = MF_REAL8;
    prop->output_mode = MF_SINGLE;
    prop->phase_state = MF_STATE0;

    int tag_idx = 0;
    for (; tag_idx < 3; ++tag_idx) {
      memcpy(tag, prop_buf + 16 + tag_idx * 8, 8);

      if (!strcmp(tag, "INT1    ")) {
        prop->data_type = MF_INT1;
      } else if (!strcmp(tag, "INT2    ")) {
        prop->data_type = MF_INT2;
      } else if (!strcmp(tag, "INT4    ")) {
        prop->data_type = MF_INT4;
      } else if (!strcmp(tag, "REAL4   ")) {
        prop->data_type = MF_REAL4;
      } else if (!strcmp(tag, "REAL8   ")) {
        prop->data_type = MF_REAL8;
      } else if (!strcmp(tag, "CHAR4   ")) {
        prop->data_type = MF_CHAR4;
      } else if (!strcmp(tag, "CHAR8   ")) {
        prop->data_type = MF_CHAR8;
      } else if (!strcmp(tag, "SINGLE  ")) {
        prop->output_mode = MF_SINGLE;
      } else if (!strcmp(tag, "DOUBLE  ")) {
        prop->output_mode = MF_DOUBLE;
      } else if (!strcmp(tag, "STATE0  ")) {
        prop->phase_state = MF_STATE0;
      } else if (!strcmp(tag, "STATE1  ")) {
        prop->phase_state = MF_STATE1;
      } else if (!strcmp(tag, "ENDITEM ")) {
        break;
      } else {
        fprintf(stderr, "Error: unknown tag '%s'\n", tag);
        err = MF_ERROR_INVALID_FILE;
        goto on_prop_error;
      }
    }

    prop_buf += 16 + (tag_idx + 1) * 8;
  }

  err = read_header(&header, stream);
  if (err != MF_OK) {
    goto on_prop_error;
  }

  assert(!strcmp(header.name, "DATA    "));
  assert(header.size >= 0);

  *offset = ftell(stream);
  *size = header.size;

  fseek(stream, (long)header.size + 16, SEEK_CUR);

  free(buf);

  return MF_OK;

on_prop_error:
  free(arrays->properties);
  arrays->properties = NULL;

on_buf_error:
  free(buf);

on_error:
  return err;
}

static void free_arrays(mf_arrays_t *arrays) {
  if (arrays->properties) {
    free(arrays->properties);
  }
  free(arrays);
}

void free_sum_description(mf_sum_description_t *desc) {
  if (desc->time) {
    free(desc->time);
  }
  if (desc->date) {
    free(desc->date);
  }
  if (desc->celldata) {
    free_arrays(desc->celldata);
  }
  if (desc->conndata) {
    free_arrays(desc->conndata);
  }
  if (desc->srcdata) {
    free_arrays(desc->srcdata);
  }
  if (desc->fpcedata) {
    free_arrays(desc->fpcedata);
  }
  if (desc->fpcodata) {
    free_arrays(desc->fpcodata);
  }
}

static int elem_size(mf_data_type_t type) {
  switch (type) {
  case MF_INT1:
    return 1;
  case MF_INT2:
    return 2;
  case MF_INT4:
  case MF_REAL4:
  case MF_CHAR4:
    return 4;
  case MF_REAL8:
  case MF_CHAR8:
    return 8;
  }
  return -1;
}

static mf_status_t read_data(FILE *stream, const mf_arrays_t *desc, const mf_sum_block_query_t *query, mf_data_t *data,
                             int64_t offset) {
  if (desc->num_properties < query->num_items) {
    fprintf(stderr, "Error: number of requested properties exceeds number of "
                    "properties inside block\n");
    return MF_ERROR_INVALID_READ_REQUEST;
  }
  assert(query->num_items >= 0);
  if (query->num_items == 0) {
    return MF_OK;
  }

  int32_t *req_indices = calloc(desc->num_properties, sizeof(int32_t));
  fill_array(req_indices, desc->num_properties, -1);
  int *element_sizes = calloc(desc->num_properties, sizeof(int));

  mf_status_t err = MF_OK;
  int32_t max_count = 0;
  for (int32_t req_idx = 0; req_idx < query->num_items; ++req_idx) {
    bool found = false;
    for (int32_t prop_idx = 0; prop_idx < desc->num_properties; ++prop_idx) {
      const mf_property_t *prop = &desc->properties[prop_idx];
      if (memcmp(query->names[req_idx], prop->name, 8)) {
        continue;
      }
      req_indices[prop_idx] = req_idx;
      found = true;
      break;
    }
    if (!found) {
      fprintf(stderr, "Error: file doesn't contain property '%s'\n", query->names[req_idx]);
      err = MF_ERROR_MISSING_PROPERTY;
      goto on_error;
    }
    max_count = max_count > data[req_idx].count ? max_count : data[req_idx].count;
  }
  max_count = max_count < desc->num_objects ? max_count : desc->num_objects;

  int32_t phst_idx = -1;
  for (int32_t prop_idx = 0; prop_idx < desc->num_properties; ++prop_idx) {
    if (!memcmp(desc->properties[prop_idx].name, "PHST    ", 8)) {
      phst_idx = prop_idx;
    }
    element_sizes[prop_idx] = elem_size(desc->properties[prop_idx].data_type);
    assert(element_sizes[prop_idx] > 0);
  }

  fseek(stream, (long)offset, SEEK_SET);
  int8_t phst;
  for (int32_t obj_idx = 0; obj_idx < max_count; ++obj_idx) {
    phst = -1;
    for (int32_t prop_idx = 0; prop_idx < desc->num_properties; ++prop_idx) {
      const mf_property_t *prop = &desc->properties[prop_idx];
      if (prop_idx == phst_idx) {
        fread(&phst, sizeof(int8_t), 1, stream);
        fseek(stream, -1l, SEEK_CUR);
        if (phst <= 0) {
          phst = 1;
        }
      }

      int32_t req_idx = req_indices[prop_idx];
      if (req_idx < 0 || obj_idx >= data[req_idx].count) {
        size_t bytes_to_skip = element_sizes[prop_idx];
        if (prop->output_mode == MF_DOUBLE) {
          bytes_to_skip *= 2;
        }
        if (prop->phase_state == MF_STATE1) {
          bytes_to_skip *= phst;
        }
        fseek(stream, (long)bytes_to_skip, SEEK_CUR);
        continue;
      }

      int64_t pos = data[req_idx].stride * obj_idx;
      size_t bytes_to_read = element_sizes[prop_idx];
      if (prop->phase_state == MF_STATE1) {
        bytes_to_read *= phst;
      }

      if (prop->output_mode == MF_DOUBLE) {
        char **dst = data[req_idx].bytes;
        fread(dst[0] + pos, bytes_to_read, 1, stream);
        fread(dst[1] + pos, bytes_to_read, 1, stream);
      } else {
        char *dst = data[req_idx].bytes;
        fread(dst + pos, bytes_to_read, 1, stream);
      }
    }
  }

on_error:
  free(element_sizes);
  free(req_indices);
  return err;
}

static const char *data_type_string(mf_data_type_t type) {
  static const char *strs[7] = {
      "INT1", "INT2", "INT4", "REAL4", "REAL8", "CHAR4", "CHAR8",
  };
  return strs[type];
}

static const char *output_mode_string(mf_output_mode_t mode) { return mode == MF_SINGLE ? "SINGLE" : "DOUBLE"; }

static const char *phase_state_string(mf_phase_state_t state) { return state == MF_STATE0 ? "STATE0" : "STATE1"; }

void write_block(FILE *stream, const char *name, const mf_arrays_t *arr, mf_data_t *data) {
  printf("Info: writing block '%s'\n", name);

  int64_t block_size;

  // Arrays + Arrays size
  block_size = 8 + 8;

  // Number of properties + number of objects
  int64_t arrays_size = 4 + 4;

  // Mnemonic + dimension + tags + ENDITEM
  int64_t property_size = 8 + 8 + 3 * 8 + 8;
  arrays_size += arr->num_properties * property_size;

  block_size += arrays_size;

  // Data + data size
  block_size += 8 + 8;

  int *element_sizes = malloc(arr->num_properties * sizeof(int));
  assert(element_sizes);

  int64_t data_size = 0;
  for (int32_t idx = 0; idx < arr->num_properties; ++idx) {
    element_sizes[idx] = elem_size(arr->properties[idx].data_type);
    if (arr->properties[idx].output_mode == MF_SINGLE) {
      data_size += arr->num_objects * element_sizes[idx];
    } else {
      data_size += arr->num_objects * element_sizes[idx] * 2;
    }
  }

  block_size += data_size;

  // Enddata + enddata size
  block_size += 8 + 8;

  fwrite(name, 1, 8, stream);
  fwrite(&block_size, 8, 1, stream);

  fwrite("ARRAYS  ", 1, 8, stream);
  fwrite(&arrays_size, 8, 1, stream);

  fwrite(&arr->num_properties, 4, 1, stream);
  fwrite(&arr->num_objects, 4, 1, stream);

  for (int32_t idx = 0; idx < arr->num_properties; ++idx) {
    fwrite(arr->properties[idx].name, 1, 8, stream);
    char mnem[9];
    const char *str;

    fwrite(arr->properties[idx].dimension, 1, 8, stream);

    str = data_type_string(arr->properties[idx].data_type);
    memset(mnem, ' ', 8);
    strcpy(mnem, str);
    mnem[strlen(str)] = ' ';
    fwrite(mnem, 1, 8, stream);

    str = output_mode_string(arr->properties[idx].output_mode);
    memset(mnem, ' ', 8);
    strcpy(mnem, str);
    mnem[strlen(str)] = ' ';
    fwrite(mnem, 1, 8, stream);

    str = phase_state_string(arr->properties[idx].phase_state);
    memset(mnem, ' ', 8);
    strcpy(mnem, str);
    mnem[strlen(str)] = ' ';
    fwrite(mnem, 1, 8, stream);

    fwrite("ENDITEM ", 1, 8, stream);
  }

  fwrite("DATA    ", 1, 8, stream);
  fwrite(&data_size, 8, 1, stream);

  for (int32_t obj_idx = 0; obj_idx < arr->num_objects; ++obj_idx) {
    for (int32_t prop_idx = 0; prop_idx < arr->num_properties; ++prop_idx) {
      if (arr->properties[prop_idx].output_mode == MF_SINGLE) {
        void *buf = data[prop_idx].bytes;
        int64_t pos = obj_idx * data[prop_idx].stride;
        fwrite((char *)buf + pos, element_sizes[prop_idx], 1, stream);
      } else {
        void **buf = data[prop_idx].bytes;
        int64_t pos = obj_idx * data[prop_idx].stride;
        fwrite((char *)(buf[0]) + pos, element_sizes[prop_idx], 1, stream);
        fwrite((char *)(buf[1]) + pos, element_sizes[prop_idx], 1, stream);
      }
    }
  }

  free(element_sizes);

  fwrite("ENDDATA ", 1, 8, stream);
  block_size = 0;
  fwrite(&block_size, 8, 1, stream);
}
