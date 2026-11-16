/* Independent test oracle for the spreading decision and scan. */
#ifndef TEST_SPREADING_REFERENCE_H
#define TEST_SPREADING_REFERENCE_H

#include "bands.h"

int spreading_decision_original(const CELTMode *m, const celt_norm *X,
      int *average, int last_decision, int *hf_average, int *tapset_decision,
      int update_hf, int end, int C, int M, const int *spread_weight);

void spreading_statistics_original(const CELTMode *m, const celt_norm *X,
      int update_hf, int end, int C, int M, const int *spread_weight,
      int *sum, int *nb_bands, int *hf_sum);

#endif
