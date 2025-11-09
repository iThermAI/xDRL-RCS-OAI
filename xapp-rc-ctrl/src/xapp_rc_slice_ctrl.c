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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_struct.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_list.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/sm/rc_sm/rc_sm_id.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <microhttpd.h>
#include <json-c/json.h>

#define PORT 8080

typedef enum{
    DRX_parameter_configuration_7_6_3_1 = 1,
    SR_periodicity_configuration_7_6_3_1 = 2,
    SPS_parameters_configuration_7_6_3_1 = 3,
    Configured_grant_control_7_6_3_1 = 4,
    CQI_table_configuration_7_6_3_1 = 5,
    Slice_level_PRB_quotal_7_6_3_1 = 6,
} rc_ctrl_service_style_2_act_id_e;

typedef enum{
    HO_control = 1,
    condiitional_HO_control = 2,
    DAPS_HO_control= 3,
} rc_ctrl_service_style_3_act_id_e;

// The UE_Id will be in Hdr, not msg.
typedef enum {
    HO_INFO = 1,
    DEST_NSSAI_TEMP = 2,
    DEST_SST_TEMP= 3,
    DEST_SD_TEMP = 4,
    PLMN_Identity_TEMP = 5,
} HO_slice_level_param_id_e;

static
e2sm_rc_ctrl_hdr_frmt_1_t gen_rc_ctrl_hdr_frmt_1(ue_id_e2sm_t ue_id, uint32_t ric_style_type, uint16_t ctrl_act_id)
{
  e2sm_rc_ctrl_hdr_frmt_1_t dst = {0};

  // 6.2.2.6
  dst.ue_id = cp_ue_id_e2sm(&ue_id);

  dst.ric_style_type = ric_style_type;
  dst.ctrl_act_id = ctrl_act_id;

  return dst;
}

static
e2sm_rc_ctrl_hdr_t gen_rc_ctrl_hdr(e2sm_rc_ctrl_hdr_e hdr_frmt, ue_id_e2sm_t ue_id, uint32_t ric_style_type, uint16_t ctrl_act_id)
{
  e2sm_rc_ctrl_hdr_t dst = {0};

  if (hdr_frmt == FORMAT_1_E2SM_RC_CTRL_HDR) {
    dst.format = FORMAT_1_E2SM_RC_CTRL_HDR;
    dst.frmt_1 = gen_rc_ctrl_hdr_frmt_1(ue_id, ric_style_type, ctrl_act_id);
  } else {
    assert(0!=0 && "not implemented the fill func for this ctrl hdr frmt");
  }

  return dst;
}

typedef enum {
    RRM_Policy_Ratio_List_8_4_3_6 = 1,
    RRM_Policy_Ratio_Group_8_4_3_6 = 2,
    RRM_Policy_8_4_3_6 = 3,
    RRM_Policy_Member_List_8_4_3_6 = 4,
    RRM_Policy_Member_8_4_3_6 = 5,
    PLMN_Identity_8_4_3_6 = 6,
    S_NSSAI_8_4_3_6 = 7,
    SST_8_4_3_6 = 8,
    SD_8_4_3_6 = 9,
    Min_PRB_Policy_Ratio_8_4_3_6 = 10,
    Max_PRB_Policy_Ratio_8_4_3_6 = 11,
    Dedicated_PRB_Policy_Ratio_8_4_3_6 = 12,
} slice_level_PRB_quota_param_id_e;

static
void gen_rrm_policy_ratio_group(lst_ran_param_t* RRM_Policy_Ratio_Group,
                                const char* sst_str,
                                const char* sd_str,
                                int min_ratio_prb,
                                int dedicated_ratio_prb,
                                int max_ratio_prb)
{
  // RRM Policy Ratio Group, STRUCTURE (RRM Policy Ratio List -> RRM Policy Ratio Group)
  // lst_ran_param_t* RRM_Policy_Ratio_Group = &RRM_Policy_Ratio_List->ran_param_val.lst->lst_ran_param[0];
  // RRM_Policy_Ratio_Group->ran_param_id = RRM_Policy_Ratio_Group_8_4_3_6;
  RRM_Policy_Ratio_Group->ran_param_struct.sz_ran_param_struct = 4;
  RRM_Policy_Ratio_Group->ran_param_struct.ran_param_struct = calloc(4, sizeof(seq_ran_param_t));
  assert(RRM_Policy_Ratio_Group->ran_param_struct.ran_param_struct != NULL && "Memory exhausted");
  // RRM Policy, STRUCTURE (RRM Policy Ratio Group -> RRM Policy)
  seq_ran_param_t* RRM_Policy = &RRM_Policy_Ratio_Group->ran_param_struct.ran_param_struct[0];
  RRM_Policy->ran_param_id = RRM_Policy_8_4_3_6;
  RRM_Policy->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  RRM_Policy->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
  assert(RRM_Policy->ran_param_val.strct != NULL && "Memory exhausted");
  RRM_Policy->ran_param_val.strct->sz_ran_param_struct = 1;
  RRM_Policy->ran_param_val.strct->ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
  assert(RRM_Policy->ran_param_val.strct->ran_param_struct != NULL && "Memory exhausted");
  // RRM Policy Member List, LIST (RRM Policy -> RRM Policy Member List)
  seq_ran_param_t* RRM_Policy_Member_List = &RRM_Policy->ran_param_val.strct->ran_param_struct[0];
  RRM_Policy_Member_List->ran_param_id = RRM_Policy_Member_List_8_4_3_6;
  RRM_Policy_Member_List->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
  RRM_Policy_Member_List->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
  assert(RRM_Policy_Member_List->ran_param_val.lst != NULL && "Memory exhausted");
  RRM_Policy_Member_List->ran_param_val.lst->sz_lst_ran_param = 1;
  RRM_Policy_Member_List->ran_param_val.lst->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
  assert(RRM_Policy_Member_List->ran_param_val.lst->lst_ran_param != NULL && "Memory exhausted");
  // RRM Policy Member, STRUCTURE (RRM Policy Member List -> RRM Policy Member)
  lst_ran_param_t* RRM_Policy_Member = &RRM_Policy_Member_List->ran_param_val.lst->lst_ran_param[0];
  // RRM_Policy_Member->ran_param_id = RRM_Policy_Member_8_4_3_6;
  RRM_Policy_Member->ran_param_struct.sz_ran_param_struct = 2;
  RRM_Policy_Member->ran_param_struct.ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
  assert(RRM_Policy_Member->ran_param_struct.ran_param_struct != NULL && "Memory exhausted");
  // PLMN Identity, ELEMENT (RRM Policy Member -> PLMN Identity)
  seq_ran_param_t* PLMN_Identity = &RRM_Policy_Member->ran_param_struct.ran_param_struct[0];
  PLMN_Identity->ran_param_id = PLMN_Identity_8_4_3_6;
  PLMN_Identity->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  PLMN_Identity->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(PLMN_Identity->ran_param_val.flag_false != NULL && "Memory exhausted");
  PLMN_Identity->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  char plmnid_str[] = "00101";
  byte_array_t plmn_id = cp_str_to_ba(plmnid_str); // TODO
  PLMN_Identity->ran_param_val.flag_false->octet_str_ran.len = plmn_id.len;
  PLMN_Identity->ran_param_val.flag_false->octet_str_ran.buf = plmn_id.buf;
  // S-NSSAI, STRUCTURE (RRM Policy Member -> S-NSSAI)
  seq_ran_param_t* S_NSSAI = &RRM_Policy_Member->ran_param_struct.ran_param_struct[1];
  S_NSSAI->ran_param_id = S_NSSAI_8_4_3_6;
  S_NSSAI->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  S_NSSAI->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
  assert(S_NSSAI->ran_param_val.strct != NULL && "Memory exhausted");
  S_NSSAI->ran_param_val.strct->sz_ran_param_struct = 2;
  S_NSSAI->ran_param_val.strct->ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
  // SST, ELEMENT (S-NSSAI -> SST)
  seq_ran_param_t* SST = &S_NSSAI->ran_param_val.strct->ran_param_struct[0];
  SST->ran_param_id = SST_8_4_3_6;
  SST->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  SST->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(SST->ran_param_val.flag_false != NULL && "Memory exhausted");
  SST->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  // char sst_str[] = "1";
  byte_array_t sst = cp_str_to_ba(sst_str); //TODO
  SST->ran_param_val.flag_false->octet_str_ran.len = sst.len;
  SST->ran_param_val.flag_false->octet_str_ran.buf = sst.buf;
  // SD, ELEMENT (S-NSSAI -> SD)
  seq_ran_param_t* SD = &S_NSSAI->ran_param_val.strct->ran_param_struct[1];
  SD->ran_param_id = SD_8_4_3_6;
  SD->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  SD->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(SD->ran_param_val.flag_false != NULL && "Memory exhausted");
  SD->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  // char sd_str[] = "0";
  byte_array_t sd = cp_str_to_ba(sd_str); //TODO
  SD->ran_param_val.flag_false->octet_str_ran.len = sd.len;
  SD->ran_param_val.flag_false->octet_str_ran.buf = sd.buf;
  // Min PRB Policy Ratio, ELEMENT (RRM Policy Ratio Group -> Min PRB Policy Ratio)
  seq_ran_param_t* Min_PRB_Policy_Ratio = &RRM_Policy_Ratio_Group->ran_param_struct.ran_param_struct[1];
  Min_PRB_Policy_Ratio->ran_param_id = Min_PRB_Policy_Ratio_8_4_3_6;
  Min_PRB_Policy_Ratio->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  Min_PRB_Policy_Ratio->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(Min_PRB_Policy_Ratio->ran_param_val.flag_false != NULL && "Memory exhausted");
  Min_PRB_Policy_Ratio->ran_param_val.flag_false->type = INTEGER_RAN_PARAMETER_VALUE;
  // TODO: not handle this value in OAI
  Min_PRB_Policy_Ratio->ran_param_val.flag_false->int_ran = min_ratio_prb;
  // Max PRB Policy Ratio, ELEMENT (RRM Policy Ratio Group -> Max PRB Policy Ratio)
  seq_ran_param_t* Max_PRB_Policy_Ratio = &RRM_Policy_Ratio_Group->ran_param_struct.ran_param_struct[2];
  Max_PRB_Policy_Ratio->ran_param_id = Max_PRB_Policy_Ratio_8_4_3_6;
  Max_PRB_Policy_Ratio->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  Max_PRB_Policy_Ratio->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(Max_PRB_Policy_Ratio->ran_param_val.flag_false != NULL && "Memory exhausted");
  Max_PRB_Policy_Ratio->ran_param_val.flag_false->type = INTEGER_RAN_PARAMETER_VALUE;
  // TODO: not handle this value in OAI
  Max_PRB_Policy_Ratio->ran_param_val.flag_false->int_ran = max_ratio_prb;
  // Dedicated PRB Policy Ratio, ELEMENT (RRM Policy Ratio Group -> Dedicated PRB Policy Ratio)
  seq_ran_param_t* Dedicated_PRB_Policy_Ratio = &RRM_Policy_Ratio_Group->ran_param_struct.ran_param_struct[3];
  Dedicated_PRB_Policy_Ratio->ran_param_id = Dedicated_PRB_Policy_Ratio_8_4_3_6;
  Dedicated_PRB_Policy_Ratio->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  Dedicated_PRB_Policy_Ratio->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(Dedicated_PRB_Policy_Ratio->ran_param_val.flag_false != NULL && "Memory exhausted");
  Dedicated_PRB_Policy_Ratio->ran_param_val.flag_false->type = INTEGER_RAN_PARAMETER_VALUE;
  Dedicated_PRB_Policy_Ratio->ran_param_val.flag_false->int_ran = dedicated_ratio_prb;

  return;
}

static
void gen_rrm_policy_ratio_list(
  seq_ran_param_t* RRM_Policy_Ratio_List,
  const char* sst_str[],
  const char* sd_str[],
  const int dedicated_ratio_prb[],  
  size_t num_slices)
{
  // seq_ran_param_t* RRM_Policy_Ratio_List =  &dst.ran_param[0];
  RRM_Policy_Ratio_List->ran_param_id = RRM_Policy_Ratio_List_8_4_3_6;
  RRM_Policy_Ratio_List->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;

  RRM_Policy_Ratio_List->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
  assert(RRM_Policy_Ratio_List->ran_param_val.lst != NULL && "Memory exhausted");
  
  RRM_Policy_Ratio_List->ran_param_val.lst->sz_lst_ran_param = num_slices;
  RRM_Policy_Ratio_List->ran_param_val.lst->lst_ran_param = calloc(num_slices, sizeof(lst_ran_param_t));
  assert(RRM_Policy_Ratio_List->ran_param_val.lst->lst_ran_param != NULL && "Memory exhausted");

  for (size_t i = 0; i < num_slices; i++) {
    gen_rrm_policy_ratio_group(
      &RRM_Policy_Ratio_List->ran_param_val.lst->lst_ran_param[i],
      sst_str[i],
      sd_str[i],
      dedicated_ratio_prb[i],
      dedicated_ratio_prb[i],
      dedicated_ratio_prb[i]);
  }

  return;
}

static
void gen_HO_slice_level(seq_ran_param_t* HO_slice_level_msg, const char* sst_str, const char* sd_str)
{
  // The structure of HO slice lever msg: { HO_INFO{ PLMN, NSSAI{ SST, SD}}}
  HO_slice_level_msg->ran_param_id = HO_INFO;
  HO_slice_level_msg->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  HO_slice_level_msg->ran_param_val.strct = calloc(1, sizeof(seq_ran_param_t));
  assert(HO_slice_level_msg->ran_param_val.strct != NULL && "Memory exhausted");
  HO_slice_level_msg->ran_param_val.strct->sz_ran_param_struct = 2; // The 5 Paramaeters should be set in this list. (NSSAI(SST, SD), PLMN)
  HO_slice_level_msg->ran_param_val.strct->ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
  assert(HO_slice_level_msg->ran_param_val.strct->ran_param_struct != NULL && "Memory exhausted");

  seq_ran_param_t* PLMN_Identity = &HO_slice_level_msg->ran_param_val.strct->ran_param_struct[0];
  PLMN_Identity->ran_param_id = PLMN_Identity_TEMP;
  PLMN_Identity->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  PLMN_Identity->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(PLMN_Identity->ran_param_val.flag_false != NULL && "Memory exhausted");
  PLMN_Identity->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  char plmnid_str[] = "00101";
  byte_array_t plmn_id = cp_str_to_ba(plmnid_str); // TODO
  PLMN_Identity->ran_param_val.flag_false->octet_str_ran.len = plmn_id.len;
  PLMN_Identity->ran_param_val.flag_false->octet_str_ran.buf = plmn_id.buf;

  // NSSAI
  seq_ran_param_t* DEST_NSSAI = &HO_slice_level_msg->ran_param_val.strct->ran_param_struct[1];
  DEST_NSSAI->ran_param_id = DEST_NSSAI_TEMP;
  DEST_NSSAI->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  DEST_NSSAI->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
  assert(DEST_NSSAI->ran_param_val.strct != NULL && "Memory exhausted");
  DEST_NSSAI->ran_param_val.strct->sz_ran_param_struct = 2;
  DEST_NSSAI->ran_param_val.strct->ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
  // SST, ELEMENT (S-NSSAI -> SST)
  seq_ran_param_t* SST = &DEST_NSSAI->ran_param_val.strct->ran_param_struct[0];
  SST->ran_param_id = DEST_SST_TEMP;
  SST->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  SST->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(SST->ran_param_val.flag_false != NULL && "Memory exhausted");
  SST->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  byte_array_t sst = cp_str_to_ba(sst_str); //TODO
  SST->ran_param_val.flag_false->octet_str_ran.len = sst.len;
  SST->ran_param_val.flag_false->octet_str_ran.buf = sst.buf;

  // SD, ELEMENT (S-NSSAI -> SD)
  seq_ran_param_t* SD = &DEST_NSSAI->ran_param_val.strct->ran_param_struct[1];
  SD->ran_param_id = DEST_SD_TEMP;
  SD->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  SD->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(SD->ran_param_val.flag_false != NULL && "Memory exhausted");
  SD->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  // char sd_str[] = "0";
  byte_array_t sd = cp_str_to_ba(sd_str); //TODO
  SD->ran_param_val.flag_false->octet_str_ran.len = sd.len;
  SD->ran_param_val.flag_false->octet_str_ran.buf = sd.buf;

  return;
}

static
e2sm_rc_ctrl_msg_frmt_1_t gen_rc_ctrl_msg_frmt_1_HO_slice_level(const char* sst_str, const char* sd_str)
{
  e2sm_rc_ctrl_msg_frmt_1_t dst = {0};

  // Wo dont have any specific standard for HO Slice level. 
  // In this case we use a simple msg

  dst.sz_ran_param = 1;
  dst.ran_param = calloc(1, sizeof(seq_ran_param_t));
  assert(dst.ran_param != NULL && "Memory exhausted");
  gen_HO_slice_level(&dst.ran_param[0], sst_str, sd_str);

  return dst;
}

static
e2sm_rc_ctrl_msg_frmt_1_t gen_rc_ctrl_msg_frmt_1_slice_level_PRB_quota(
  const char* sst_str[],
  const char* sd_str[],
  const int dedicated_ratio_prb[],
  size_t num_slices)
{
  e2sm_rc_ctrl_msg_frmt_1_t dst = {0};

  // 8.4.3.6
  // RRM Policy Ratio List, LIST (len 1)
  // > RRM Policy Ratio Group, STRUCTURE (len 4)
  // >>  RRM Policy, STRUCTURE (len 1)
  // >>> RRM Policy Member List, LIST (len 1)
  // >>>> RRM Policy Member, STRUCTURE (len 2)
  // >>>>> PLMN Identity, ELEMENT
  // >>>>> S-NSSAI, STRUCTURE (len 2)
  // >>>>>> SST, ELEMENT
  // >>>>>> SD, ELEMENT
  // >> Min PRB Policy Ratio, ELEMENT
  // >> Max PRB Policy Ratio, ELEMENT
  // >> Dedicated PRB Policy Ratio, ELEMENT


  // RRM Policy Ratio List, LIST
  dst.sz_ran_param = 1;
  dst.ran_param = calloc(1, sizeof(seq_ran_param_t));
  assert(dst.ran_param != NULL && "Memory exhausted");
  gen_rrm_policy_ratio_list(&dst.ran_param[0], sst_str, sd_str, dedicated_ratio_prb, num_slices);

  return dst;
}

static
e2sm_rc_ctrl_msg_t gen_rc_ctrl_slice_level_PRB_quata_msg(
  e2sm_rc_ctrl_msg_e msg_frmt,
  const char* sst_str[],
  const char* sd_str[],
  const int dedicated_ratio_prb[],
  size_t num_slices)

{
  e2sm_rc_ctrl_msg_t dst = {0};

  if (msg_frmt == FORMAT_1_E2SM_RC_CTRL_MSG) {
    dst.format = msg_frmt;
    dst.frmt_1 = gen_rc_ctrl_msg_frmt_1_slice_level_PRB_quota(sst_str, sd_str, dedicated_ratio_prb, num_slices);
  } else {
    assert(0!=0 && "not implemented the fill func for this ctrl msg frmt");
  }

  return dst;
}

static
e2sm_rc_ctrl_msg_t gen_rc_ctrl_HO_slice_level_msg(e2sm_rc_ctrl_msg_e msg_frmt, const char* sst_str, const char* sd_str)
{
  e2sm_rc_ctrl_msg_t dst = {0};

  if (msg_frmt == FORMAT_1_E2SM_RC_CTRL_MSG) {
    dst.format = msg_frmt;
    dst.frmt_1 = gen_rc_ctrl_msg_frmt_1_HO_slice_level(sst_str, sd_str);
  } else {
    assert(0!=0 && "not implemented the fill func for this ctrl msg frmt");
  }

  return dst;
}

static
ue_id_e2sm_t gen_rc_ue_id(ue_id_e2sm_e type)
{
  ue_id_e2sm_t ue_id = {0};
  if (type == GNB_UE_ID_E2SM) {
    ue_id.type = GNB_UE_ID_E2SM;
    // TODO
    ue_id.gnb.amf_ue_ngap_id = 0;
    ue_id.gnb.guami.plmn_id.mcc = 1;
    ue_id.gnb.guami.plmn_id.mnc = 1;
    ue_id.gnb.guami.plmn_id.mnc_digit_len = 2;
    ue_id.gnb.guami.amf_region_id = 0;
    ue_id.gnb.guami.amf_set_id = 0;
    ue_id.gnb.guami.amf_ptr = 0;
  } else {
    assert(0!=0 && "not supported UE ID type");
  }

  return ue_id;
}

void HO_rc_slice_level_UE(ue_id_e2sm_t* ue_id, const char* sst_str, const char* sd_str) // Dest SST and SD
{

  // Temporary argc and argv
  int argc = 1;
  char* argv[] = {"RC_Slice", NULL}; 
  fr_args_t args = init_fr_args(argc, argv);
  //defer({ free_fr_args(&args); });

  //Init the xApp
  init_xapp_api(&args);
  sleep(1);

  e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
  defer({ free_e2_node_arr_xapp(&nodes); });
  assert(nodes.len > 0);
  printf("[xApp]: Connected E2 nodes = %d\n", nodes.len);

  ////////////
  // START RC
  ////////////

  // RC Control
  // CONTROL Service Style 3: Connected Mode Mobility Control
  // Action ID 1: Handover Control
  // E2SM-RC Control Header Format 1
  // E2SM-RC Control Message Format 1
  rc_ctrl_req_data_t rc_ctrl = {0};
  // ue_id_e2sm_t ue_id = gen_rc_ue_id(GNB_UE_ID_E2SM);  // find the specific UE!!!!!

  rc_ctrl.hdr = gen_rc_ctrl_hdr(FORMAT_1_E2SM_RC_CTRL_HDR, *ue_id, 3, HO_control);
  rc_ctrl.msg = gen_rc_ctrl_HO_slice_level_msg(FORMAT_1_E2SM_RC_CTRL_MSG, sst_str, sd_str);


  // We have to find the source node and send Ho request to it. 
  // Wo wont do it in this case, We send the request to all cells.
  for(size_t i =0; i < nodes.len; ++i){
    control_sm_xapp_api(&nodes.n[i].id, SM_RC_ID, &rc_ctrl);
  }
  free_rc_ctrl_req_data(&rc_ctrl);
}


// ======================================== REST API Functions ========================================

void run_rc_control_task(const char* sst_str[], const char* sd_str[],
                         const int dedicated_ratio_prb[], size_t num_slices);

struct connection_info {
  char *body;
  size_t size;
};

int handle_request(void *cls, struct MHD_Connection *connection,
                   const char *url, const char *method,
                   const char *version, const char *upload_data,
                   size_t *upload_data_size, void **con_cls)
{
  // Allocate per-connection structure
  if (*con_cls == NULL) {
    struct connection_info *info = calloc(1, sizeof(struct connection_info));
    *con_cls = info;
    return MHD_YES;
  }

  struct connection_info *info = *con_cls;

  // Accumulate POST data
  if (strcmp(method, "POST") == 0 && *upload_data_size > 0) {
    info->body = realloc(info->body, info->size + *upload_data_size + 1);
    memcpy(info->body + info->size, upload_data, *upload_data_size);
    info->size += *upload_data_size;
    info->body[info->size] = '\0';
    *upload_data_size = 0;
    return MHD_YES;
  }

  // When upload finished (*upload_data_size == 0), process JSON
  if (strcmp(method, "POST") == 0 && info->body != NULL) {
    if (strcmp(url, "/run") != 0) {
      const char *msg = "Unknown endpoint\nAvailable endpoints: ( /run )\n";
      struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(msg), (void*)msg, MHD_RESPMEM_PERSISTENT);
      int ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, resp);
      MHD_destroy_response(resp);
      free(info->body);
      free(info);
      *con_cls = NULL;
      return ret;
    }

    // Parse JSON body safely
    struct json_object *parsed = json_tokener_parse(info->body);
    if (!parsed) {
      const char *msg = "Invalid JSON structure\n";
      struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(msg), (void*)msg, MHD_RESPMEM_PERSISTENT);
      int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
      MHD_destroy_response(resp);
      free(info->body);
      free(info);
      *con_cls = NULL;
      return ret;
    }

    // Extract sst, sd, dedicated_ratio_prb here
    struct json_object *sst_array = json_object_object_get(parsed, "sst");
    struct json_object *sd_array  = json_object_object_get(parsed, "sd");
    struct json_object *ratio_array = json_object_object_get(parsed, "dedicated_ratio_prb");

    if (!sst_array || !sd_array || !ratio_array) {
      const char *msg = "Missing required arrays\nAll arrays must be provided: ( sst, sd, dedicated_ratio_prb )\n";
      struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(msg), (void*)msg, MHD_RESPMEM_PERSISTENT);
      int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, resp);
      MHD_destroy_response(resp);
      json_object_put(parsed);
      free(info->body);
      free(info);
      *con_cls = NULL;
      return ret;
    }

    size_t num_slices = json_object_array_length(sst_array);
    const char **sst_str = calloc(num_slices, sizeof(char*));
    const char **sd_str = calloc(num_slices, sizeof(char*));
    int *dedicated_ratio_prb = calloc(num_slices, sizeof(int));

    for (size_t i = 0; i < num_slices; i++) {
      sst_str[i] = strdup(json_object_get_string(json_object_array_get_idx(sst_array, i)));
      sd_str[i]  = strdup(json_object_get_string(json_object_array_get_idx(sd_array, i)));
      dedicated_ratio_prb[i] = json_object_get_int(json_object_array_get_idx(ratio_array, i));
    }

    // Call run rc function
    run_rc_control_task(sst_str, sd_str, dedicated_ratio_prb, num_slices);

    // Cleanup
    for (size_t i = 0; i < num_slices; i++) {
      free((void*)sst_str[i]);
      free((void*)sd_str[i]);
    }
    free(sst_str);
    free(sd_str);
    free(dedicated_ratio_prb);
    json_object_put(parsed);

    // Send response
    const char *resp_text = "Task executed successfully\n";
    struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(resp_text), (void*)resp_text, MHD_RESPMEM_PERSISTENT);
    int ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    free(info->body);
    free(info);
    *con_cls = NULL;
    return ret;
  }

  return MHD_NO;
}

// Function that performs RC control task
void run_rc_control_task(const char* sst_str[], const char* sd_str[],
                         const int dedicated_ratio_prb[], size_t num_slices)
{
  printf("[xApp]: Running RC Control with %zu slices\n", num_slices);
  for (size_t i = 0; i < num_slices; i++) {
    printf("  Slice %zu -> sst=%s, sd=%s, ratio=%d\n", i, sst_str[i], sd_str[i], dedicated_ratio_prb[i]);
  }
  
  e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
  defer({ free_e2_node_arr_xapp(&nodes); });

  if (nodes.len == 0) {
    printf("[xApp]: No connected nodes.\n");
    return;
  }

  printf("[xApp]: Connected E2 nodes = %d\n", nodes.len);

  rc_ctrl_req_data_t rc_ctrl = {0};
  ue_id_e2sm_t ue_id = gen_rc_ue_id(GNB_UE_ID_E2SM);

  // Slice creation logic
  rc_ctrl.hdr = gen_rc_ctrl_hdr(FORMAT_1_E2SM_RC_CTRL_HDR, ue_id, 2, Slice_level_PRB_quotal_7_6_3_1);
  rc_ctrl.msg = gen_rc_ctrl_slice_level_PRB_quata_msg(FORMAT_1_E2SM_RC_CTRL_MSG, sst_str, sd_str, dedicated_ratio_prb, num_slices);

  for(size_t i = 0; i < nodes.len; ++i){
    control_sm_xapp_api(&nodes.n[i].id, SM_RC_ID, &rc_ctrl);
  }

  free_rc_ctrl_req_data(&rc_ctrl);
}

// Thread to run the REST server
void* rest_server_thread(void* arg)
{
  struct MHD_Daemon *daemon;
  daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL, &handle_request, NULL, MHD_OPTION_END);
  if (daemon == NULL) {
    fprintf(stderr, "[xApp]: Failed to start REST server\n");
    return NULL;
  }
  printf("[xApp]: REST API running on port %d\n", PORT);
  while (1) sleep(1);
  MHD_stop_daemon(daemon);
  return NULL;
}

// ======================================== REST API Functions ========================================

int main(int argc, char *argv[])
{
  fr_args_t args = init_fr_args(argc, argv);
  init_xapp_api(&args);
  sleep(1);

  printf("[xApp]: Ready and waiting for REST API calls.\n");

  pthread_t rest_thread;
  pthread_create(&rest_thread, NULL, rest_server_thread, NULL);

  // Keep main loop alive
  while(1)
    sleep(1);

  printf("[xApp]: xApp shutting down.\n");
  return 0;
}
