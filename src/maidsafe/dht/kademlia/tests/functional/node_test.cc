/* Copyright (c) 2009 maidsafe.net limited All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
    * Neither the name of the maidsafe.net limited nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstdint>
#include <functional>
#include <exception>
#include <list>
#include <set>
#include <vector>

#include "boost/asio.hpp"
#ifdef __MSVC__
#  pragma warning(push)
#  pragma warning(disable: 4127)
#endif
#include "boost/date_time/posix_time/posix_time.hpp"
#ifdef __MSVC__
#  pragma warning(pop)
#endif
#include "boost/lexical_cast.hpp"

#include "maidsafe/common/test.h"
#include "maidsafe/common/crypto.h"
#include "maidsafe/dht/log.h"
// #include "maidsafe-dht/common/routing_table.h"
#include "maidsafe/dht/kademlia/config.h"
#include "maidsafe/dht/kademlia/node-api.h"
#include "maidsafe/dht/kademlia/node_impl.h"
#include "maidsafe/dht/transport/transport.h"
#include "maidsafe/dht/transport/tcp_transport.h"
#include "maidsafe/dht/kademlia/message_handler.h"
#include "maidsafe/dht/kademlia/securifier.h"
#include "maidsafe/dht/kademlia/return_codes.h"
#include "maidsafe/dht/kademlia/tests/test_utils.h"
#include "maidsafe/dht/kademlia/node_container.h"
#include "maidsafe/dht/kademlia/tests/functional/test_node_environment.h"

namespace arg = std::placeholders;
namespace fs = boost::filesystem;
namespace bptime = boost::posix_time;

namespace maidsafe {
namespace dht {
namespace kademlia {
namespace test {

void MultiNodeFindValueCallback(
    FindValueReturns find_value_returns_in,
    boost::mutex *mutex,
    boost::condition_variable *cond_var,
    std::vector<FindValueReturns> *find_value_returns_container) {
  boost::mutex::scoped_lock lock(*mutex);
  find_value_returns_container->push_back(find_value_returns_in);
  cond_var->notify_one();
}

bool MultiNodeFindValueResultReady(
    size_t *sent_count,
    std::vector<FindValueReturns> *find_value_returns_container) {
  return *sent_count == find_value_returns_container->size();
}

class NodeTest : public testing::Test {
 protected:
  typedef std::shared_ptr<maidsafe::dht::kademlia::NodeContainer<Node>>
      NodeContainerPtr;
  NodeTest()
      : env_(NodesEnvironment<Node>::g_environment()),
        kTimeout_(transport::kDefaultInitialTimeout +
                  transport::kDefaultInitialTimeout),
        chosen_node_index_(RandomUint32() % env_->node_containers_.size()),
        chosen_container_(env_->node_containers_[chosen_node_index_]) {}

  NodesEnvironment<Node>* env_;
  const bptime::time_duration kTimeout_;
  size_t chosen_node_index_;
  NodeContainerPtr chosen_container_;
 private:
  NodeTest(const NodeTest&);
  NodeTest& operator=(const NodeTest&);
};

TEST_F(NodeTest, FUNC_InvalidBootstrapContact) {
  std::vector<Contact> bootstrap_contacts;
  for (int index = 0; index < 3; ++index) {
    NodeContainerPtr node_container(
        new maidsafe::dht::kademlia::NodeContainer<Node>());
    node_container->Init(3, SecurifierPtr(),
        AlternativeStorePtr(new TestNodeAlternativeStore), false, env_->k_,
        env_->alpha_, env_->beta_, env_->mean_refresh_interval_);
    node_container->MakeAllCallbackFunctors(&env_->mutex_, &env_->cond_var_);
    bootstrap_contacts.push_back(node_container->node()->contact());
  }
  NodeContainerPtr node_container(
      new maidsafe::dht::kademlia::NodeContainer<Node>());
  node_container->Init(3, SecurifierPtr(),
      AlternativeStorePtr(new TestNodeAlternativeStore), false, env_->k_,
      env_->alpha_, env_->beta_, env_->mean_refresh_interval_);
  node_container->MakeAllCallbackFunctors(&env_->mutex_, &env_->cond_var_);
  /*int result = */node_container->Start(bootstrap_contacts, 8000);
  ASSERT_FALSE(node_container->node()->joined());
}

TEST_F(NodeTest, FUNC_JoinClient) {
  NodeContainerPtr client_node_container(
      new maidsafe::dht::kademlia::NodeContainer<Node>());
  client_node_container->Init(3, SecurifierPtr(),
      AlternativeStorePtr(new TestNodeAlternativeStore), true, env_->k_,
      env_->alpha_, env_->beta_, env_->mean_refresh_interval_);
  client_node_container->MakeAllCallbackFunctors(&env_->mutex_,
                                                 &env_->cond_var_);
  std::vector<Contact> bootstrap_contacts;
  (*env_->node_containers_.rbegin())->node()->
      GetBootstrapContacts(&bootstrap_contacts);
  int result = client_node_container->Start(bootstrap_contacts, 0);
  ASSERT_EQ(kSuccess, result);
  ASSERT_TRUE(client_node_container->node()->joined());
}

TEST_F(NodeTest, FUNC_StoreAndFindSmallValue) {
  const Key kKey(Key::kRandomId);
  const std::string kValue(RandomString(1024));
  int result(kGeneralError);
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->Store(kKey, kValue, "", bptime::pos_infin,
                             chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_store_functor()));
    chosen_container_->GetAndResetStoreResult(&result);
  }
  ASSERT_EQ(kSuccess, result);

  FindValueReturns find_value_returns;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->FindValue(kKey, chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_find_value_functor()));
    chosen_container_->GetAndResetFindValueResult(&find_value_returns);
  }
  EXPECT_EQ(kSuccess, find_value_returns.return_code);
  ASSERT_EQ(1U, find_value_returns.values.size());
  EXPECT_EQ(kValue, find_value_returns.values.front());
  EXPECT_TRUE(find_value_returns.closest_nodes.empty());
  EXPECT_EQ(Contact(), find_value_returns.alternative_store_holder);
  EXPECT_NE(Contact(), find_value_returns.needs_cache_copy);
}

TEST_F(NodeTest, FUNC_StoreAndFindBigValue) {
  const Key kKey(Key::kRandomId);
  const std::string kValue(RandomString(1024 * 1024));
  int result(kGeneralError);
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->Store(kKey, kValue, "", bptime::pos_infin,
                             chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_store_functor()));
    chosen_container_->GetAndResetStoreResult(&result);
  }
  ASSERT_EQ(kSuccess, result);

  FindValueReturns find_value_returns;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->FindValue(kKey, chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_find_value_functor()));
    chosen_container_->GetAndResetFindValueResult(&find_value_returns);
  }
  EXPECT_EQ(kSuccess, find_value_returns.return_code);
  ASSERT_EQ(1U, find_value_returns.values.size());
  EXPECT_EQ(kValue, find_value_returns.values.front());
  EXPECT_TRUE(find_value_returns.closest_nodes.empty());
  EXPECT_EQ(Contact(), find_value_returns.alternative_store_holder);
  EXPECT_NE(Contact(), find_value_returns.needs_cache_copy);
}

TEST_F(NodeTest, FUNC_StoreAndFindMultipleValues) {
  const size_t kValueCount(100);
  std::vector<Key> keys;
  keys.reserve(kValueCount);
  std::vector<std::string> values;
  values.reserve(kValueCount);
  int result(kGeneralError);
  for (size_t i(0); i != kValueCount; ++i) {
    result = kGeneralError;
    keys.push_back(Key(crypto::Hash<crypto::SHA512>(
        boost::lexical_cast<std::string>(i))));
    values.push_back(RandomString((RandomUint32() % 1024) + 1024));
    {
      boost::mutex::scoped_lock lock(env_->mutex_);
      chosen_container_->Store(keys.back(), values.back(), "",
                               bptime::pos_infin,
                               chosen_container_->securifier());
      EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                  chosen_container_->wait_for_store_functor()));
      chosen_container_->GetAndResetStoreResult(&result);
    }
    ASSERT_EQ(kSuccess, result);
  }

  FindValueReturns find_value_returns;
  auto key_itr(keys.begin());
  auto value_itr(values.begin());
  for (; key_itr != keys.end(); ++key_itr, ++value_itr) {
    find_value_returns = FindValueReturns();
    {
      boost::mutex::scoped_lock lock(env_->mutex_);
      chosen_container_->FindValue(*key_itr, chosen_container_->securifier());
      EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                  chosen_container_->wait_for_find_value_functor()));
      chosen_container_->GetAndResetFindValueResult(&find_value_returns);
    }
    EXPECT_EQ(kSuccess, find_value_returns.return_code);
    ASSERT_EQ(1U, find_value_returns.values.size());
    EXPECT_EQ(*value_itr, find_value_returns.values.front());
    EXPECT_TRUE(find_value_returns.closest_nodes.empty());
    EXPECT_EQ(Contact(), find_value_returns.alternative_store_holder);
    EXPECT_NE(Contact(), find_value_returns.needs_cache_copy);
  }
}

TEST_F(NodeTest, FUNC_MultipleNodesFindSingleValue) {
  const Key kKey(Key::kRandomId);
  const std::string kValue(RandomString(1024));
  int result(kGeneralError);
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->Store(kKey, kValue, "", bptime::pos_infin,
                             chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_store_functor()));
    chosen_container_->GetAndResetStoreResult(&result);
  }
  ASSERT_EQ(kSuccess, result);

  std::vector<FindValueReturns> find_value_returns_container;
  // Replace default FindValue callbacks with one for this test
  for (auto it(env_->node_containers_.begin());
       it != env_->node_containers_.end(); ++it) {
    (*it)->set_find_value_functor(std::bind(&MultiNodeFindValueCallback,
                                            arg::_1, &env_->mutex_,
                                            &env_->cond_var_,
                                            &find_value_returns_container));
  }

  // Send all requests and wait for all to return
  size_t sent_count(0);
  boost::mutex::scoped_lock lock(env_->mutex_);
  for (size_t i(0); i < env_->node_containers_.size(); i += 2) {
    ++sent_count;
    env_->node_containers_[i]->FindValue(kKey,
        env_->node_containers_[i]->securifier());
  }
  EXPECT_TRUE(env_->cond_var_.timed_wait(lock, bptime::minutes(2),
              std::bind(&MultiNodeFindValueResultReady, &sent_count,
                        &find_value_returns_container)));

  // Assess results
  EXPECT_FALSE(find_value_returns_container.empty());
  for (auto it(find_value_returns_container.begin());
       it != find_value_returns_container.end(); ++it) {
    EXPECT_EQ(kSuccess, (*it).return_code);
    ASSERT_EQ(1U, (*it).values.size());
    EXPECT_EQ(kValue, (*it).values.front());
    EXPECT_TRUE((*it).closest_nodes.empty());
    EXPECT_EQ(Contact(), (*it).alternative_store_holder);
    EXPECT_NE(Contact(), (*it).needs_cache_copy);
  }
}

TEST_F(NodeTest, FUNC_ClientFindValue) {
  NodeContainerPtr client_node_container(
      new maidsafe::dht::kademlia::NodeContainer<Node>());
  client_node_container->Init(3, SecurifierPtr(),
      AlternativeStorePtr(new TestNodeAlternativeStore), true, env_->k_,
      env_->alpha_, env_->beta_, env_->mean_refresh_interval_);
  client_node_container->MakeAllCallbackFunctors(&env_->mutex_,
                                                 &env_->cond_var_);
  std::vector<Contact> bootstrap_contacts;
  (*env_->node_containers_.rbegin())->node()->
      GetBootstrapContacts(&bootstrap_contacts);
  int result = client_node_container->Start(bootstrap_contacts, 0);
  ASSERT_EQ(kSuccess, result);
  ASSERT_TRUE(client_node_container->node()->joined());

  const Key kKey(Key::kRandomId);
  const std::string kValue(RandomString(1024));
  result = kGeneralError;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    client_node_container->Store(kKey, kValue, "", bptime::pos_infin,
                                 client_node_container->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                client_node_container->wait_for_store_functor()));
    result = kGeneralError;
    client_node_container->GetAndResetStoreResult(&result);
  }
  EXPECT_EQ(kSuccess, result);

  FindValueReturns find_value_returns;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    client_node_container->FindValue(kKey, client_node_container->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                client_node_container->wait_for_find_value_functor()));
    client_node_container->GetAndResetFindValueResult(&find_value_returns);
  }
  EXPECT_EQ(kSuccess, find_value_returns.return_code);
  ASSERT_EQ(1U, find_value_returns.values.size());
  EXPECT_EQ(kValue, find_value_returns.values.front());
  EXPECT_TRUE(find_value_returns.closest_nodes.empty());
  EXPECT_EQ(Contact(), find_value_returns.alternative_store_holder);
  EXPECT_NE(Contact(), find_value_returns.needs_cache_copy);
}

TEST_F(NodeTest, FUNC_GetContact) {
  size_t target_index(RandomUint32() % env_->node_containers_.size());
  while (chosen_node_index_ == target_index)
    target_index = RandomUint32() % env_->node_containers_.size();
  NodeContainerPtr target_container(env_->node_containers_[target_index]);

  boost::mutex::scoped_lock lock(env_->mutex_);
  chosen_container_->GetContact(target_container->node()->contact().node_id());
  EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
              chosen_container_->wait_for_get_contact_functor()));
  int result(kGeneralError);
  Contact returned_contact;
  chosen_container_->GetAndResetGetContactResult(&result, &returned_contact);
  EXPECT_EQ(kSuccess, result);
  EXPECT_EQ(target_container->node()->contact(), returned_contact);
}

TEST_F(NodeTest, FUNC_FindNonExistingValue) {
  const Key kKey(Key::kRandomId);
  FindValueReturns find_value_returns;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->FindValue(kKey, chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_find_value_functor()));
    chosen_container_->GetAndResetFindValueResult(&find_value_returns);
  }
  EXPECT_EQ(kIterativeLookupFailed, find_value_returns.return_code);
  EXPECT_TRUE(find_value_returns.values.empty());
  EXPECT_FALSE(find_value_returns.closest_nodes.empty());
  EXPECT_EQ(Contact(), find_value_returns.alternative_store_holder);
  EXPECT_NE(Contact(), find_value_returns.needs_cache_copy);
}

TEST_F(NodeTest, FUNC_FindDeadNode) {
  size_t target_index(RandomUint32() % env_->node_containers_.size());
  while (chosen_node_index_ == target_index)
    target_index = RandomUint32() % env_->node_containers_.size();
  NodeContainerPtr target_container(env_->node_containers_[target_index]);
  target_container->Stop(NULL);

  boost::mutex::scoped_lock lock(env_->mutex_);
  chosen_container_->FindNodes(target_container->node()->contact().node_id());
  EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
              chosen_container_->wait_for_find_nodes_functor()));
  int result(kGeneralError);
  std::vector<Contact> closest_nodes;
  chosen_container_->GetAndResetFindNodesResult(&result, &closest_nodes);
  EXPECT_EQ(kSuccess, result);
  EXPECT_TRUE(std::find(closest_nodes.begin(), closest_nodes.end(),
              target_container->node()->contact()) == closest_nodes.end());
}

TEST_F(NodeTest, FUNC_JoinLeave)  {
  EXPECT_TRUE(chosen_container_->node()->joined());
  std::vector<Contact> bootstrap_contacts;
  chosen_container_->node()->Leave(&bootstrap_contacts);
  EXPECT_FALSE(chosen_container_->node()->joined());
  EXPECT_FALSE(bootstrap_contacts.empty());

  boost::mutex::scoped_lock lock(env_->mutex_);
  chosen_container_->Join(chosen_container_->node()->contact().node_id(),
                          bootstrap_contacts);
  EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
              chosen_container_->wait_for_join_functor()));
  int result(kGeneralError);
  chosen_container_->GetAndResetJoinResult(&result);
  EXPECT_EQ(kSuccess, result);
  EXPECT_TRUE(chosen_container_->node()->joined());
}

TEST_F(NodeTest, FUNC_StoreWithInvalidRequest) {
  const Key kKey(Key::kRandomId);
  const std::string kValue(RandomString(1024));
  int result(kGeneralError);
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->Store(kKey, kValue, "", bptime::pos_infin,
                             chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_store_functor()));
    chosen_container_->GetAndResetStoreResult(&result);
  }
  ASSERT_EQ(kSuccess, result);

  NodeContainerPtr next_container(env_->node_containers_[
      (chosen_node_index_ + 1) % env_->node_containers_.size()]);
  const std::string kAnotherValue(RandomString(1024));
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    next_container->Store(kKey, kAnotherValue, "", bptime::pos_infin,
                          next_container->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                next_container->wait_for_store_functor()));
    next_container->GetAndResetStoreResult(&result);
  }
  EXPECT_EQ(kStoreTooFewNodes, result);
}

TEST_F(NodeTest, FUNC_Update) {
  const Key kKey(Key::kRandomId);
  const std::string kValue(RandomString(1024));
  int result(kGeneralError);
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->Store(kKey, kValue, "", bptime::pos_infin,
                             chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_store_functor()));
    chosen_container_->GetAndResetStoreResult(&result);
  }
  ASSERT_EQ(kSuccess, result);

  FindValueReturns find_value_returns;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->FindValue(kKey, chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_find_value_functor()));
    chosen_container_->GetAndResetFindValueResult(&find_value_returns);
  }
  EXPECT_EQ(kSuccess, find_value_returns.return_code);
  ASSERT_EQ(1U, find_value_returns.values.size());
  EXPECT_EQ(kValue, find_value_returns.values.front());
  EXPECT_TRUE(find_value_returns.closest_nodes.empty());
  EXPECT_EQ(Contact(), find_value_returns.alternative_store_holder);
  EXPECT_NE(Contact(), find_value_returns.needs_cache_copy);

  const std::string kAnotherValue(RandomString(1024));
  ASSERT_NE(kValue, kAnotherValue);
  result = kGeneralError;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->Update(kKey, kAnotherValue, "", kValue, "",
                              bptime::pos_infin,
                              chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_update_functor()));
    chosen_container_->GetAndResetUpdateResult(&result);
  }
  EXPECT_EQ(kSuccess, result);

  find_value_returns = FindValueReturns();
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->FindValue(kKey, chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_find_value_functor()));
    chosen_container_->GetAndResetFindValueResult(&find_value_returns);
  }
  EXPECT_EQ(kSuccess, find_value_returns.return_code);
  ASSERT_EQ(1U, find_value_returns.values.size());
  EXPECT_EQ(kAnotherValue, find_value_returns.values.front());
  EXPECT_TRUE(find_value_returns.closest_nodes.empty());
  EXPECT_EQ(Contact(), find_value_returns.alternative_store_holder);
  EXPECT_NE(Contact(), find_value_returns.needs_cache_copy);
}

TEST_F(NodeTest, FUNC_FindNodes) {
  const NodeId kTargetId(NodeId::kRandomId);
  int result(kGeneralError);
  std::vector<Contact> closest_nodes;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->FindNodes(kTargetId);
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_find_nodes_functor()));
    chosen_container_->GetAndResetFindNodesResult(&result, &closest_nodes);
  }
  EXPECT_EQ(kSuccess, result);
  EXPECT_EQ(env_->k_, closest_nodes.size());
  for (auto it(closest_nodes.begin()); it != closest_nodes.end(); ++it) {
    EXPECT_TRUE(WithinKClosest((*it).node_id(), kTargetId, env_->node_ids_,
                env_->k_));
  }
}

TEST_F(NodeTest, FUNC_Delete) {
  const Key kKey(Key::kRandomId);
  const std::string kValue(RandomString(1024));
  int result(kGeneralError);
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->Store(kKey, kValue, "", bptime::pos_infin,
                             chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_store_functor()));
    chosen_container_->GetAndResetStoreResult(&result);
  }
  ASSERT_EQ(kSuccess, result);

  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->Delete(kKey, kValue, "",
                              chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_delete_functor()));
    int result(kGeneralError);
    chosen_container_->GetAndResetDeleteResult(&result);
  }
  EXPECT_EQ(kSuccess, result);

  FindValueReturns find_value_returns;
  {
    boost::mutex::scoped_lock lock(env_->mutex_);
    chosen_container_->FindValue(kKey, chosen_container_->securifier());
    EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
                chosen_container_->wait_for_find_value_functor()));
    chosen_container_->GetAndResetFindValueResult(&find_value_returns);
  }
  EXPECT_EQ(kGeneralError, find_value_returns.return_code);
  EXPECT_TRUE(find_value_returns.values.empty());
  EXPECT_FALSE(find_value_returns.closest_nodes.empty());
  EXPECT_EQ(Contact(), find_value_returns.alternative_store_holder);
  EXPECT_EQ(Contact(), find_value_returns.needs_cache_copy);
}

TEST_F(NodeTest, FUNC_InvalidDeleteRequest) {
  const Key kKey(Key::kRandomId);
  const std::string kValue(RandomString(1024));
  boost::mutex::scoped_lock lock(env_->mutex_);
  chosen_container_->Delete(kKey, kValue, "", chosen_container_->securifier());
  EXPECT_TRUE(env_->cond_var_.timed_wait(lock, kTimeout_,
              chosen_container_->wait_for_delete_functor()));
  int result(kGeneralError);
  chosen_container_->GetAndResetDeleteResult(&result);
  EXPECT_EQ(kDeleteTooFewNodes, result);
}

}  // namespace test
}  // namespace kademlia
}  // namespace dht
}  // namespace maidsafe