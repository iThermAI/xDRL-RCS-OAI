/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BAS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/util/alg_ds/alg/defer.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/util/e.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <mysql/mysql.h>

volatile sig_atomic_t sig_recv = 0;

static
uint64_t const period_ms = 1000;

static
pthread_mutex_t mtx;

void signal_handler_thread(void *arg){
  sigset_t signal_set;
  int sig_number;

  // Block SIGINT and SIGTERM
  sigemptyset(&signal_set);
  sigaddset(&signal_set, SIGINT);
  sigaddset(&signal_set, SIGTERM);

  if(sigwait(&signal_set, &sig_number) == 0) {
    printf("Received %d, cleaning up", sig_number);
    sig_recv = 1;
  }
}

// ======================================== MySql Functions ========================================

// Define a structure to hold KPI metrics
typedef struct {
    double rru_prb_tot_dl;
    double rru_prb_tot_ul;
    double drb_pdcp_sdu_volume_dl;
    double drb_pdcp_sdu_volume_ul;
    double drb_rlc_sdu_delay_dl;
    double drb_ue_thp_dl;
    double drb_ue_thp_ul;
    unsigned long amf_ue_ngap_id;
    unsigned long ran_ue_id;
} kpi_metrics_t;

// Initialize a static variable to hold the metrics
static kpi_metrics_t kpi_metrics = {0};

static MYSQL* conn = NULL;

static void init_database() {
    const char* host = getenv("DB_HOST");
    if (!host) host = "127.0.0.1";

    const char* user = getenv("DB_USER");
    if (!user) user = "admin";

    const char* password = getenv("DB_PASSWORD");
    if (!password) password = "password";

    const char* database = getenv("DB_NAME");
    if (!database) database = "flexric_db";

    const char* port_str = getenv("DB_PORT");
    unsigned int port = 3307;
    if (port_str) port = (unsigned int) atoi(port_str);

    // Initialize MySQL connection
    conn = mysql_init(NULL);
    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        exit(EXIT_FAILURE);
    }

    // Connect to the database
    if (mysql_real_connect(conn, host, user, password, database, port, NULL, 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        exit(EXIT_FAILURE);
    }

    // SQL statement to create the table
    const char* sql = "CREATE TABLE IF NOT EXISTS xapp_kpi_metrics ("
                      "id INT AUTO_INCREMENT PRIMARY KEY, "
                      "rru_prb_tot_dl DOUBLE, "
                      "rru_prb_tot_ul DOUBLE, "
                      "drb_pdcp_sdu_volume_dl DOUBLE, "
                      "drb_pdcp_sdu_volume_ul DOUBLE, "
                      "drb_rlc_sdu_delay_dl DOUBLE, "
                      "drb_ue_thp_dl DOUBLE, "
                      "drb_ue_thp_ul DOUBLE, "
                      "amf_ue_ngap_id BIGINT, "
                      "ran_ue_id BIGINT, "
                      "timestamp BIGINT NOT NULL);";

    // Execute the SQL statement
    if (mysql_query(conn, sql)) {
        fprintf(stderr, "create table failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        exit(EXIT_FAILURE);
    }

    printf("database and table initialized successfully.\n");
}

// Function to insert the metrics into the database
static void insert_to_database() {
    if (conn == NULL) {
        fprintf(stderr, "database connection is not initialized.\n");
        return;
    }

    // Get the current timestamp
    time_t now = time(NULL);

    // Construct the SQL query
    char query[1024];
    snprintf(query, sizeof(query),
            "INSERT INTO xapp_kpi_metrics (rru_prb_tot_dl, rru_prb_tot_ul, drb_pdcp_sdu_volume_dl, "
            "drb_pdcp_sdu_volume_ul, drb_rlc_sdu_delay_dl, drb_ue_thp_dl, drb_ue_thp_ul, "
            "amf_ue_ngap_id, ran_ue_id, timestamp) "
            "VALUES (%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %lu, %lu, %ld);",
            kpi_metrics.rru_prb_tot_dl, kpi_metrics.rru_prb_tot_ul,
            kpi_metrics.drb_pdcp_sdu_volume_dl, kpi_metrics.drb_pdcp_sdu_volume_ul,
            kpi_metrics.drb_rlc_sdu_delay_dl, kpi_metrics.drb_ue_thp_dl, kpi_metrics.drb_ue_thp_ul,
            kpi_metrics.amf_ue_ngap_id, kpi_metrics.ran_ue_id, now);

    // Execute the query
    if (mysql_query(conn, query)) {
        fprintf(stderr, "insert failed: %s\n", mysql_error(conn));
    } else {
        printf("metrics inserted successfully.\n");
    }
}

// Function to close the MySQL connection
static void close_database() {
    if (conn != NULL) {
        mysql_close(conn);
        printf("database connection closed.\n");
    }
}

// ======================================== MySql Functions ========================================

static
void log_gnb_ue_id(ue_id_e2sm_t ue_id)
{
  if (ue_id.gnb.gnb_cu_ue_f1ap_lst != NULL) {
    for (size_t i = 0; i < ue_id.gnb.gnb_cu_ue_f1ap_lst_len; i++) {
      printf("UE ID type = gNB-CU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb.gnb_cu_ue_f1ap_lst[i]);
    }
  } else {
    printf("UE ID type = gNB, amf_ue_ngap_id = %lu\n", ue_id.gnb.amf_ue_ngap_id);
    kpi_metrics.amf_ue_ngap_id = ue_id.gnb.amf_ue_ngap_id;
  }
  if (ue_id.gnb.ran_ue_id != NULL) {
    printf("ran_ue_id = %lx\n", *ue_id.gnb.ran_ue_id); // RAN UE NGAP ID
    kpi_metrics.ran_ue_id = *ue_id.gnb.ran_ue_id;
  }
}

static
void log_du_ue_id(ue_id_e2sm_t ue_id)
{
  printf("UE ID type = gNB-DU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb_du.gnb_cu_ue_f1ap);
  if (ue_id.gnb_du.ran_ue_id != NULL) {
    printf("ran_ue_id = %lx\n", *ue_id.gnb_du.ran_ue_id); // RAN UE NGAP ID
  }
}

static
void log_cuup_ue_id(ue_id_e2sm_t ue_id)
{
  printf("UE ID type = gNB-CU-UP, gnb_cu_cp_ue_e1ap = %u\n", ue_id.gnb_cu_up.gnb_cu_cp_ue_e1ap);
  if (ue_id.gnb_cu_up.ran_ue_id != NULL) {
    printf("ran_ue_id = %lx\n", *ue_id.gnb_cu_up.ran_ue_id); // RAN UE NGAP ID
  }
}

typedef void (*log_ue_id)(ue_id_e2sm_t ue_id);

static
log_ue_id log_ue_id_e2sm[END_UE_ID_E2SM] = {
    log_gnb_ue_id, // common for gNB-mono, CU and CU-CP
    log_du_ue_id,
    log_cuup_ue_id,
    NULL,
    NULL,
    NULL,
    NULL,
};

// Update the metric values based on the measurement type and value
static void update_metrics(byte_array_t name, meas_record_lst_t meas_record) {
  if (cmp_str_ba("RRU.PrbTotDl", name) == 0) {
    kpi_metrics.rru_prb_tot_dl = (double)meas_record.int_val;

  } else if (cmp_str_ba("RRU.PrbTotUl", name) == 0) {
    kpi_metrics.rru_prb_tot_ul = (double)meas_record.int_val;

  } else if (cmp_str_ba("DRB.PdcpSduVolumeDL", name) == 0) {
    kpi_metrics.drb_pdcp_sdu_volume_dl = (double)meas_record.int_val;

  } else if (cmp_str_ba("DRB.PdcpSduVolumeUL", name) == 0) {
    kpi_metrics.drb_pdcp_sdu_volume_ul = (double)meas_record.int_val;

  } else if (cmp_str_ba("DRB.RlcSduDelayDl", name) == 0) {
    kpi_metrics.drb_rlc_sdu_delay_dl = meas_record.real_val;

  } else if (cmp_str_ba("DRB.UEThpDl", name) == 0) {
    kpi_metrics.drb_ue_thp_dl = meas_record.real_val;

  } else if (cmp_str_ba("DRB.UEThpUl", name) == 0) {
    kpi_metrics.drb_ue_thp_ul = meas_record.real_val;
      
  } else {
    printf("Measurement Name not yet supported: %.*s\n", (int)name.len, name.buf);
  }
}

static
void log_int_value(byte_array_t name, meas_record_lst_t meas_record)
{
  update_metrics(name, meas_record);
}

static
void log_real_value(byte_array_t name, meas_record_lst_t meas_record)
{
  update_metrics(name, meas_record);
}

typedef void (*log_meas_value)(byte_array_t name, meas_record_lst_t meas_record);

static
log_meas_value get_meas_value[END_MEAS_VALUE] = {
  log_int_value,
  log_real_value,
  NULL,
};

static
void match_meas_name_type(meas_type_t meas_type, meas_record_lst_t meas_record)
{
  // Get the value of the Measurement
  get_meas_value[meas_record.value](meas_type.name, meas_record);
}

static
void match_id_meas_type(meas_type_t meas_type, meas_record_lst_t meas_record)
{
  (void)meas_type;
  (void)meas_record;
  assert(false && "ID Measurement Type not yet supported");
}

typedef void (*check_meas_type)(meas_type_t meas_type, meas_record_lst_t meas_record);

static
check_meas_type match_meas_type[END_MEAS_TYPE] = {
  match_meas_name_type,
  match_id_meas_type,
};

static
void log_kpm_measurements(kpm_ind_msg_format_1_t const* msg_frm_1)
{
  assert(msg_frm_1->meas_info_lst_len > 0 && "Cannot correctly print measurements");

  // Process measurements
  for (size_t j = 0; j < msg_frm_1->meas_data_lst_len; j++) {
    meas_data_lst_t const data_item = msg_frm_1->meas_data_lst[j];

    for (size_t z = 0; z < data_item.meas_record_len; z++) {
      meas_type_t const meas_type = msg_frm_1->meas_info_lst[z].meas_type;
      meas_record_lst_t const record_item = data_item.meas_record_lst[z];

      match_meas_type[meas_type.type](meas_type, record_item);

      if (data_item.incomplete_flag && *data_item.incomplete_flag == TRUE_ENUM_VALUE) {
        printf("Measurement Record not reliable\n");
      }
    }
  }
  // Insert the metrics into the database after processing all measurements
  insert_to_database();
}

static
void sm_cb_kpm(sm_ag_if_rd_t const* rd)
{
  assert(rd != NULL);
  assert(rd->type == INDICATION_MSG_AGENT_IF_ANS_V0);
  assert(rd->ind.type == KPM_STATS_V3_0);

  // Reading Indication Message Format 3
  kpm_ind_data_t const* ind = &rd->ind.kpm.ind;
  kpm_ric_ind_hdr_format_1_t const* hdr_frm_1 = &ind->hdr.kpm_ric_ind_hdr_format_1;
  kpm_ind_msg_format_3_t const* msg_frm_3 = &ind->msg.frm_3;

  int64_t const now = time_now_us();
  static int counter = 1;
  {
    lock_guard(&mtx);

    printf("\n%7d KPM ind_msg latency = %ld [μs]\n", counter, now - hdr_frm_1->collectStartTime); // xApp <-> E2 Node

    // Reported list of measurements per UE
    for (size_t i = 0; i < msg_frm_3->ue_meas_report_lst_len; i++) {
      // log UE ID
      ue_id_e2sm_t const ue_id_e2sm = msg_frm_3->meas_report_per_ue[i].ue_meas_report_lst;
      ue_id_e2sm_e const type = ue_id_e2sm.type;
      log_ue_id_e2sm[type](ue_id_e2sm);

      // log measurements
      log_kpm_measurements(&msg_frm_3->meas_report_per_ue[i].ind_msg_format_1);
      
    }
    counter++;
  }
}

static
test_info_lst_t filter_predicate(test_cond_type_e type, test_cond_e cond, const int value[])
{
  test_info_lst_t dst = {0};

  dst.test_cond_type = type;
  // It can only be TRUE_TEST_COND_TYPE so it does not matter the type
  // but ugly ugly...
  dst.S_NSSAI = TRUE_TEST_COND_TYPE;

  dst.test_cond = calloc(1, sizeof(test_cond_e));
  assert(dst.test_cond != NULL && "Memory exhausted");
  *dst.test_cond = cond;

  dst.test_cond_value = calloc(1, sizeof(test_cond_value_t));
  assert(dst.test_cond_value != NULL && "Memory exhausted");
  dst.test_cond_value->type = OCTET_STRING_TEST_COND_VALUE;

  dst.test_cond_value->octet_string_value = calloc(1, sizeof(byte_array_t));
  assert(dst.test_cond_value->octet_string_value != NULL && "Memory exhausted");
  const size_t len_nssai = 4;
  dst.test_cond_value->octet_string_value->len = len_nssai;
  dst.test_cond_value->octet_string_value->buf = calloc(len_nssai, sizeof(uint8_t));
  assert(dst.test_cond_value->octet_string_value->buf != NULL && "Memory exhausted");
  dst.test_cond_value->octet_string_value->buf[0] = value[0];
  dst.test_cond_value->octet_string_value->buf[1] = value[1];
  dst.test_cond_value->octet_string_value->buf[2] = value[2];
  dst.test_cond_value->octet_string_value->buf[3] = value[3];

  return dst;
}

static
label_info_lst_t fill_kpm_label(void)
{
  label_info_lst_t label_item = {0};

  label_item.noLabel = ecalloc(1, sizeof(enum_value_e));
  *label_item.noLabel = TRUE_ENUM_VALUE;

  return label_item;
}

static
kpm_act_def_format_1_t fill_act_def_frm_1(ric_report_style_item_t const* report_item)
{
  assert(report_item != NULL);

  kpm_act_def_format_1_t ad_frm_1 = {0};

  size_t const sz = report_item->meas_info_for_action_lst_len;

  // [1, 65535]
  ad_frm_1.meas_info_lst_len = sz;
  ad_frm_1.meas_info_lst = calloc(sz, sizeof(meas_info_format_1_lst_t));
  assert(ad_frm_1.meas_info_lst != NULL && "Memory exhausted");

  for (size_t i = 0; i < sz; i++) {
    meas_info_format_1_lst_t* meas_item = &ad_frm_1.meas_info_lst[i];
    // 8.3.9
    // Measurement Name
    meas_item->meas_type.type = NAME_MEAS_TYPE;
    meas_item->meas_type.name = copy_byte_array(report_item->meas_info_for_action_lst[i].name);

    // [1, 2147483647]
    // 8.3.11
    meas_item->label_info_lst_len = 1;
    meas_item->label_info_lst = ecalloc(1, sizeof(label_info_lst_t));
    meas_item->label_info_lst[0] = fill_kpm_label();
  }

  // 8.3.8 [0, 4294967295]
  ad_frm_1.gran_period_ms = period_ms;

  // 8.3.20 - OPTIONAL
  ad_frm_1.cell_global_id = NULL;

#if defined KPM_V2_03 || defined KPM_V3_00
  // [0, 65535]
  ad_frm_1.meas_bin_range_info_lst_len = 0;
  ad_frm_1.meas_bin_info_lst = NULL;
#endif

  return ad_frm_1;
}

static
kpm_act_def_t fill_report_style_4(ric_report_style_item_t const* report_item, const int *value)
{
  assert(report_item != NULL);
  assert(report_item->act_def_format_type == FORMAT_4_ACTION_DEFINITION);

  kpm_act_def_t act_def = {.type = FORMAT_4_ACTION_DEFINITION};

  // Fill matching condition
  // [1, 32768]
  act_def.frm_4.matching_cond_lst_len = 1;
  act_def.frm_4.matching_cond_lst = calloc(act_def.frm_4.matching_cond_lst_len, sizeof(matching_condition_format_4_lst_t));
  assert(act_def.frm_4.matching_cond_lst != NULL && "Memory exhausted");
  // Filter connected UEs by S-NSSAI criteria
  test_cond_type_e const type = S_NSSAI_TEST_COND_TYPE; // CQI_TEST_COND_TYPE
  test_cond_e const condition = EQUAL_TEST_COND; // GREATERTHAN_TEST_COND

  act_def.frm_4.matching_cond_lst[0].test_info_lst = filter_predicate(type, condition, value);

  // Fill Action Definition Format 1
  // 8.2.1.2.1
  act_def.frm_4.action_def_format_1 = fill_act_def_frm_1(report_item);

  return act_def;
}

typedef kpm_act_def_t (*fill_kpm_act_def)(ric_report_style_item_t const* report_item, const int *value);

static
fill_kpm_act_def get_kpm_act_def[END_RIC_SERVICE_REPORT] = {
    NULL,
    NULL,
    NULL,
    fill_report_style_4,
    NULL,
};

static
kpm_sub_data_t gen_kpm_subs(kpm_ran_function_def_t const* ran_func, const int *value)
{
  assert(ran_func != NULL);
  assert(ran_func->ric_event_trigger_style_list != NULL);

  kpm_sub_data_t kpm_sub = {0};

  // Generate Event Trigger
  assert(ran_func->ric_event_trigger_style_list[0].format_type == FORMAT_1_RIC_EVENT_TRIGGER);
  kpm_sub.ev_trg_def.type = FORMAT_1_RIC_EVENT_TRIGGER;
  kpm_sub.ev_trg_def.kpm_ric_event_trigger_format_1.report_period_ms = period_ms;

  // Generate Action Definition
  kpm_sub.sz_ad = 1;
  kpm_sub.ad = calloc(kpm_sub.sz_ad, sizeof(kpm_act_def_t));
  assert(kpm_sub.ad != NULL && "Memory exhausted");

  // Multiple Action Definitions in one SUBSCRIPTION message is not supported in this project
  // Multiple REPORT Styles = Multiple Action Definition = Multiple SUBSCRIPTION messages
  ric_report_style_item_t* const report_item = &ran_func->ric_report_style_list[0];
  ric_service_report_e const report_style_type = report_item->report_style_type;
  *kpm_sub.ad = get_kpm_act_def[report_style_type](report_item, value);

  return kpm_sub;
}

static
bool eq_sm(sm_ran_function_t const* elem, int const id)
{
  if (elem->id == id)
    return true;

  return false;
}

static
size_t find_sm_idx(sm_ran_function_t* rf, size_t sz, bool (*f)(sm_ran_function_t const*, int const), int const id)
{
  for (size_t i = 0; i < sz; i++) {
    if (f(&rf[i], id))
      return i;
  }

  assert(0 != 0 && "SM ID could not be found in the RAN Function List");
}

int main(int argc, char* argv[])
{
  // Initialize the database
  init_database();

  fr_args_t args = init_fr_args(argc, argv);
  pthread_t thread;
  sigset_t signal_set;

  // Init the xApp
  init_xapp_api(&args);
  sleep(1);

  e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
  defer({ free_e2_node_arr_xapp(&nodes); });

  assert(nodes.len > 0);

  printf("Connected E2 nodes = %d\n", nodes.len);

  pthread_mutexattr_t attr = {0};
  int rc = pthread_mutex_init(&mtx, &attr);
  assert(rc == 0);

  sm_ans_xapp_t* hndl = calloc(3*nodes.len, sizeof(sm_ans_xapp_t));
  assert(hndl != NULL);

  ////////////
  // START KPM
  ////////////
  int const KPM_ran_function = 2;

  for (size_t i = 0; i < nodes.len; ++i) {
    e2_node_connected_xapp_t* n = &nodes.n[i];

    size_t const idx = find_sm_idx(n->rf, n->len_rf, eq_sm, KPM_ran_function);
    assert(n->rf[idx].defn.type == KPM_RAN_FUNC_DEF_E && "KPM is not the received RAN Function");
    // if REPORT Service is supported by E2 node, send SUBSCRIPTION
    // e.g. OAI CU-CP
    if (n->rf[idx].defn.kpm.ric_report_style_list != NULL) {
      // Define S-NSSAIs: {sst, sd16, sd8, sd0}
      const int nassai_list[][4] = {
        {128, 0x00, 0x00, 0x80},  // sst=128, sd=0x000080
        {1,   0x00, 0x00, 0x01},  // sst=1,   sd=0x000001
        {5,   0x00, 0x00, 0x82}   // sst=5,   sd=0x000082
      };
      const int num_slices = 3;
      for (int s = 0; s < num_slices; s++) {
        kpm_sub_data_t kpm_sub = gen_kpm_subs(&n->rf[idx].defn.kpm, nassai_list[s]);
        hndl[i] = report_sm_xapp_api(&n->id, KPM_ran_function, &kpm_sub, sm_cb_kpm);
        assert(hndl[i].success == true);
        free_kpm_sub_data(&kpm_sub);
      }
    }
  }
  ////////////
  // END KPM
  ////////////

  // Block SIGINT and SIGTERM in main thread (so only sigwait can handle them)
  sigemptyset(&signal_set);
  sigaddset(&signal_set, SIGINT);
  sigaddset(&signal_set, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

  pthread_create(&thread, NULL, signal_handler_thread, NULL);

  while(!sig_recv){
    sleep(1);
  }

  for (int i = 0; i < 3*nodes.len; ++i) {
    // Remove the handle previously returned
    if (hndl[i].success == true)
      rm_report_sm_xapp_api(hndl[i].u.handle);
  }
  free(hndl);

  // Stop the xApp
  while (try_stop_xapp_api() == false)
    usleep(1000);

  close_database();

  printf("Test xApp run SUCCESSFULLY\n");
}
