// Copyright (c) 2017, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <unordered_set>
#include "include_base_utils.h"
#include "crypto/crypto.h"
#include "ringct/rctOps.h"
#include "cryptonote_basic/account.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "multisig.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "multisig"

using namespace std;

namespace cryptonote
{
  //-----------------------------------------------------------------
  bool generate_key_image_helper_old(const account_keys& ack, const crypto::public_key& tx_public_key, size_t real_output_index, keypair& in_ephemeral, crypto::key_image& ki)
  {
    crypto::key_derivation recv_derivation = AUTO_VAL_INIT(recv_derivation);
    bool r = crypto::generate_key_derivation(tx_public_key, ack.m_view_secret_key, recv_derivation);
    CHECK_AND_ASSERT_MES(r, false, "key image helper: failed to generate_key_derivation(" << tx_public_key << ", " << ack.m_view_secret_key << ")");

    r = crypto::derive_public_key(recv_derivation, real_output_index, ack.m_account_address.m_spend_public_key, in_ephemeral.pub);
    CHECK_AND_ASSERT_MES(r, false, "key image helper: failed to derive_public_key(" << recv_derivation << ", " << real_output_index <<  ", " << ack.m_account_address.m_spend_public_key << ")");

    crypto::derive_secret_key(recv_derivation, real_output_index, ack.m_spend_secret_key, in_ephemeral.sec);

    crypto::generate_key_image(in_ephemeral.pub, in_ephemeral.sec, ki);
    return true;
  }
  //-----------------------------------------------------------------
  void generate_multisig_N_N(const account_keys &keys, const std::vector<crypto::public_key> &spend_keys, std::vector<crypto::secret_key> &multisig_keys, rct::key &spend_skey, rct::key &spend_pkey)
  {
    // the multisig spend public key is the sum of all spend public keys
    multisig_keys.clear();
    spend_pkey = rct::pk2rct(keys.m_account_address.m_spend_public_key);
    for (const auto &k: spend_keys)
      rct::addKeys(spend_pkey, spend_pkey, rct::pk2rct(k));
    multisig_keys.push_back(keys.m_spend_secret_key);
    spend_skey = rct::sk2rct(keys.m_spend_secret_key);
  }
  //-----------------------------------------------------------------
  void generate_multisig_N1_N(const account_keys &keys, const std::vector<crypto::public_key> &spend_keys, std::vector<crypto::secret_key> &multisig_keys, rct::key &spend_skey, rct::key &spend_pkey)
  {
    multisig_keys.clear();
    spend_pkey = rct::identity();
    spend_skey = rct::zero();

    // create all our composite private keys
    for (const auto &k: spend_keys)
    {
      rct::keyV data;
      data.push_back(rct::scalarmultKey(rct::pk2rct(k), rct::sk2rct(keys.m_spend_secret_key)));
      static const rct::key salt = { {'M', 'u', 'l', 't' , 'i', 's', 'i', 'g' , 0x00, 0x00, 0x00,0x00 , 0x00, 0x00, 0x00,0x00 , 0x00, 0x00, 0x00,0x00 , 0x00, 0x00, 0x00,0x00 , 0x00, 0x00, 0x00,0x00 , 0x00, 0x00, 0x00,0x00  } };
      data.push_back(salt);
      rct::key msk = rct::hash_to_scalar(data);
      multisig_keys.push_back(rct::rct2sk(msk));
      sc_add(spend_skey.bytes, spend_skey.bytes, msk.bytes);
    }
  }
  //-----------------------------------------------------------------
  crypto::secret_key generate_multisig_view_secret_key(const crypto::secret_key &skey, const std::vector<crypto::secret_key> &skeys)
  {
    crypto::hash hash;
    crypto::cn_fast_hash(&skey, sizeof(crypto::hash), hash);
    rct::key view_skey = rct::hash2rct(hash);
    for (const auto &k: skeys)
      sc_add(view_skey.bytes, view_skey.bytes, rct::sk2rct(k).bytes);
    return rct::rct2sk(view_skey);
  }
  //-----------------------------------------------------------------
  crypto::public_key generate_multisig_N1_N_spend_public_key(const std::vector<crypto::public_key> &pkeys)
  {
    rct::key spend_public_key = rct::identity();
    for (const auto &pk: pkeys)
    {
      rct::addKeys(spend_public_key, spend_public_key, rct::pk2rct(pk));
    }
    return rct::rct2pk(spend_public_key);
  }
  //-----------------------------------------------------------------
  bool generate_multisig_key_image(const account_keys &keys, const crypto::public_key& tx_public_key, size_t real_output_index, cryptonote::keypair& in_ephemeral, crypto::key_image& ki, size_t multisig_key_index)
  {
    if (multisig_key_index >= keys.m_multisig_keys.size())
      return false;
    if (!cryptonote::generate_key_image_helper_old(keys, tx_public_key, real_output_index, in_ephemeral, ki))
      return false;
    // we got the ephemeral keypair, but the key image isn't right as it's done as per our private spend key, which is multisig
    crypto::generate_key_image(in_ephemeral.pub, keys.m_multisig_keys[multisig_key_index], ki);
    return true;
  }
  //-----------------------------------------------------------------
  void generate_multisig_LR(const crypto::public_key pkey, const crypto::secret_key &k, crypto::public_key &L, crypto::public_key &R)
  {
    rct::scalarmultBase((rct::key&)L, rct::sk2rct(k));
    crypto::generate_key_image(pkey, k, (crypto::key_image&)R);
  }
  //-----------------------------------------------------------------
  bool generate_multisig_composite_key_image(const account_keys &keys, const crypto::public_key &tx_public_key, size_t real_output_index, const std::vector<crypto::key_image> &pkis, crypto::key_image &ki)
  {
    cryptonote::keypair in_ephemeral;
    if (!cryptonote::generate_key_image_helper_old(keys, tx_public_key, real_output_index, in_ephemeral, ki))
      return false;
    std::unordered_set<crypto::key_image> used;
    for (size_t m = 0; m < keys.m_multisig_keys.size(); ++m)
    {
      crypto::key_image pki;
      bool r = cryptonote::generate_multisig_key_image(keys, tx_public_key, real_output_index, in_ephemeral, pki, m);
      if (!r)
        return false;
      used.insert(pki);
    }
    for (const auto &pki: pkis)
    {
      if (used.find(pki) == used.end())
      {
        used.insert(pki);
        rct::addKeys((rct::key&)ki, rct::ki2rct(ki), rct::ki2rct(pki));
      }
    }
    return true;
  }
  //-----------------------------------------------------------------
}
