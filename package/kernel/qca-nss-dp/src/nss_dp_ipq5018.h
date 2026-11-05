/* SPDX-License-Identifier: ISC */
#ifndef NSS_DP_IPQ5018_PHYLINK_H
#define NSS_DP_IPQ5018_PHYLINK_H

#include "nss_dp_dev.h"

#ifdef NSS_DP_IPQ50XX
int nss_dp_ipq5018_init(struct nss_dp_dev *dp);
int nss_dp_ipq5018_connect(struct nss_dp_dev *dp);
void nss_dp_ipq5018_cleanup(struct nss_dp_dev *dp);
void nss_dp_ipq5018_start(struct nss_dp_dev *dp);
void nss_dp_ipq5018_stop(struct nss_dp_dev *dp);
void nss_dp_ipq5018_set_ethtool(struct nss_dp_dev *dp);
#else
static inline int nss_dp_ipq5018_init(struct nss_dp_dev *dp) { return 0; }
static inline int nss_dp_ipq5018_connect(struct nss_dp_dev *dp) { return 0; }
static inline void nss_dp_ipq5018_cleanup(struct nss_dp_dev *dp) { }
static inline void nss_dp_ipq5018_start(struct nss_dp_dev *dp) { }
static inline void nss_dp_ipq5018_stop(struct nss_dp_dev *dp) { }
static inline void nss_dp_ipq5018_set_ethtool(struct nss_dp_dev *dp) { }
#endif
#endif
