#include "mufitsio.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *sim_name;
  const char *sum_dir;
  const char *out_dir;
  long id_start;
  long id_end;
} app_config;

static void print_help();
static bool parse_arguments(int argc, const char **argv, app_config *cfg);
static int num_digits(long n);
static bool run(const app_config *cfg);
static bool read_num_cells(const char *mvs_file_path, int32_t *num_cells);
static void remap_ids(int32_t *ids, const int32_t num_cells);
static void sort_field(double *field, const int32_t *ids, const int32_t num_cells);
static bool convert_sum_file(const char *sum_file_path, const char *out_file_path, const int32_t num_cells);

int main(int argc, const char **argv) {
  if (argc < 6) {
    fprintf(stderr, "Error: not enough arguments\n");
    print_help();
    return EXIT_FAILURE;
  }

  app_config cfg;
  if (!parse_arguments(argc, argv, &cfg)) {
    return EXIT_FAILURE;
  }

  if (!run(&cfg)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

void print_help() {
  printf("Usage:\n"
         "  mufits2matlab <sim-name> <path-to-sum-dir> <path-to-out-dir> <id-start> <id-end>\n\n"
         "    <sim-name>        : name of the RUN file without extension\n"
         "    <path-to-sum-dir> : path to the directory containing SUM files and MVS file\n"
         "    <path-to-out-dir> : path to the directory where files for MATLAB will be written\n"
         "    <id-start>        : first time step\n"
         "    <id-end>          : last time step\n");
}

bool parse_arguments(int argc, const char **argv, app_config *cfg) {
  cfg->sim_name = argv[1];
  cfg->sum_dir = argv[2];
  cfg->out_dir = argv[3];

  char *str_end;

  cfg->id_start = strtol(argv[4], &str_end, 10);
  if (str_end == argv[4] || *str_end != '\0') {
    fprintf(stderr, "Error: id-start must be valid integer\n");
    return false;
  }
  if (errno == ERANGE) {
    fprintf(stderr, "Error: id-start out of range\n");
    return false;
  }

  cfg->id_end = strtol(argv[5], &str_end, 10);
  if (str_end == argv[5] || *str_end != '\0') {
    fprintf(stderr, "Error: id-end must be valid integer\n");
    return false;
  }
  if (errno == ERANGE) {
    fprintf(stderr, "Error: id-end out of range\n");
    return false;
  }

  // Sanity checks
  if (cfg->id_start < 0) {
    fprintf(stderr, "Error: id-start must be non-negative\n");
    return false;
  }
  if (cfg->id_end < 0) {
    fprintf(stderr, "Error: id-end must be non-negative\n");
    return false;
  }
  if (cfg->id_end < cfg->id_start) {
    fprintf(stderr, "Error: id-start must not exceed id-end\n");
    return false;
  }

  return true;
}

int num_digits(long n) {
  int d = 0;
  do {
    n /= 10;
    d++;
  } while (n > 0);
  return d;
}

bool run(const app_config *cfg) {
  int32_t num_cells;
  // 4 for '.MVS', 1 for '\0'
  char *mvs_file_path = malloc(strlen(cfg->sum_dir) + 1 + strlen(cfg->sim_name) + 4 + 1);
  sprintf(mvs_file_path, "%s/%s.MVS", cfg->sum_dir, cfg->sim_name);
  if (!read_num_cells(mvs_file_path, &num_cells)) {
    free(mvs_file_path);
    return false;
  }
  if (num_cells <= 0) {
    fprintf(stderr, "Error: number of cells in MVS file less than zero\n");
    free(mvs_file_path);
    return false;
  }
  free(mvs_file_path);

  int nd = num_digits(cfg->id_end);
  if (nd < 4) {
    nd = 4;
  }
  // 1 for '/', 1 for '.', 4 for '.SUM' or '.dat', 1 for '\0'
  char *sum_file_path = malloc(strlen(cfg->sum_dir) + 1 + strlen(cfg->sim_name) + 1 + nd + 4 + 1);
  char *out_file_path = malloc(strlen(cfg->out_dir) + 1 + strlen(cfg->sim_name) + 1 + nd + 4 + 1);
  for (long it = cfg->id_start; it <= cfg->id_end; ++it) {
    sprintf(sum_file_path, "%s/%s.%0*ld.SUM", cfg->sum_dir, cfg->sim_name, nd, it);
    sprintf(out_file_path, "%s/%s.%0*ld.dat", cfg->out_dir, cfg->sim_name, nd, it);
    printf("  Converting file '%s'\n", sum_file_path);
    if (!convert_sum_file(sum_file_path, out_file_path, num_cells)) {
      free(out_file_path);
      free(sum_file_path);
      return false;
    }
  }
  free(out_file_path);
  free(sum_file_path);
  return true;
}

bool read_num_cells(const char *mvs_file_path, int32_t *num_cells) {
  mf_mvs_file_t *mvs;
  if (mf_open_mvs_file(&mvs, mvs_file_path) != MF_OK) {
    return false;
  }

  mf_mvs_description_t *desc = mf_get_mvs_description(mvs);
  *num_cells = desc->num_cells;

  mf_close_mvs_file(mvs);
  return true;
}

static int int32_pair_cmp(const void *v1, const void *v2) { return *((int32_t *)v1) - *((int32_t *)v2); }

void remap_ids(int32_t *ids, const int32_t num_cells) {
  int32_t *sorter = malloc(2 * num_cells * sizeof(int32_t));
  for (int32_t idx = 0; idx < num_cells; ++idx) {
    sorter[2 * idx + 0] = ids[idx];
    sorter[2 * idx + 1] = idx;
  }
  qsort(sorter, num_cells, 2 * sizeof(int32_t), int32_pair_cmp);
  int32_t *map = calloc((sorter[2 * (num_cells - 1)] + 1), sizeof(int32_t));
  for (int32_t idx = 0; idx < num_cells; ++idx) {
    map[sorter[2 * idx]] = idx;
  }
  for (int32_t idx = 0; idx < num_cells; ++idx) {
    ids[idx] = map[ids[idx]];
  }
  free(map);
  free(sorter);
}

void sort_field(double *field, const int32_t *ids, const int32_t num_cells) {
  double *tmp = malloc(num_cells * sizeof(double));
  memcpy(tmp, field, num_cells * sizeof(double));
  for (int32_t idx = 0; idx < num_cells; ++idx) {
    field[ids[idx]] = tmp[idx];
  }
  free(tmp);
}

bool convert_sum_file(const char *sum_file_path, const char *out_file_path, const int32_t num_cells) {
  mf_sum_file_t *sum;
  if (mf_open_sum_file(&sum, sum_file_path) != MF_OK) {
    return false;
  }

  mf_sum_description_t *desc = mf_get_sum_description(sum);

  if (desc->celldata == NULL) {
    fprintf(stderr, "Error: CELLDATA is missing\n");
    mf_close_sum_file(sum);
    return false;
  }

  int32_t file_num_cells = desc->celldata->num_objects;

  int32_t *cell_id = malloc(file_num_cells * sizeof(int32_t));
  double *pressure = malloc(file_num_cells * sizeof(double));
  double *temperature = malloc(file_num_cells * sizeof(double));

  mf_sum_block_query_t celldata_query;
  char celldata_names[][9] = {"CELLID  ", "PRES    ", "TEMP    "};

  celldata_query.names = celldata_names;
  celldata_query.num_items = 3;

  mf_data_t celldata_destinations[] = {
      {.bytes = cell_id, .stride = sizeof(int32_t), .count = file_num_cells},
      {.bytes = pressure, .stride = sizeof(double), .count = file_num_cells},
      {.bytes = temperature, .stride = sizeof(double), .count = file_num_cells},
  };

  mf_sum_attachment_t sum_attachment = {0};
  sum_attachment.celldata = celldata_destinations;

  mf_sum_read_request_t sum_request = {0};
  sum_request.celldata = &celldata_query;

  if (mf_read_sum_file(sum, &sum_request, &sum_attachment) != MF_OK) {
    mf_close_sum_file(sum);
    free(cell_id);
    free(pressure);
    free(temperature);
    return false;
  }

  mf_close_sum_file(sum);

  for (int32_t idx = 0; idx < file_num_cells; ++idx) {
    cell_id[idx]--;
  }

  remap_ids(cell_id, file_num_cells);
  sort_field(pressure, cell_id, file_num_cells);
  sort_field(temperature, cell_id, file_num_cells);

  // Convert pressure to Pa
  for (int32_t idx = 0; idx < file_num_cells; ++idx) {
    pressure[idx] *= 1e5;
  }

  FILE *fid = fopen(out_file_path, "wb");
  if (fid == NULL) {
    printf("Failed to open file %s\n", out_file_path);
    perror("System error");
    free(cell_id);
    free(pressure);
    free(temperature);
    return false;
  }

  fwrite(pressure, sizeof(double), num_cells, fid);
  fwrite(temperature, sizeof(double), num_cells, fid);
  fclose(fid);

  free(cell_id);
  free(pressure);
  free(temperature);
  return true;
}
