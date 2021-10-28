/*
 *  Copyright (c) 2020 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Project: curve
 * Created Date: 20211010
 * Author: xuchaojie,lixiaocui
 */

#include <gtest/gtest.h>
#include <glog/logging.h>

#include "src/common/lru_cache.h"
#include "src/common/timeutility.h"

namespace curve {
namespace common {

TEST(TestCacheMetrics, testall) {
    CacheMetrics cacheMetrics("LRUCache");

    // 1. 新增数据项
    cacheMetrics.UpdateAddToCacheCount();
    ASSERT_EQ(1, cacheMetrics.cacheCount.get_value());

    cacheMetrics.UpdateAddToCacheBytes(1000);
    ASSERT_EQ(1000, cacheMetrics.cacheBytes.get_value());

    // 2. 移除数据项
    cacheMetrics.UpdateRemoveFromCacheCount();
    ASSERT_EQ(0, cacheMetrics.cacheCount.get_value());

    cacheMetrics.UpdateRemoveFromCacheBytes(200);
    ASSERT_EQ(800, cacheMetrics.cacheBytes.get_value());

    // 3. cache命中
    ASSERT_EQ(0, cacheMetrics.cacheHit.get_value());
    cacheMetrics.OnCacheHit();
    ASSERT_EQ(1, cacheMetrics.cacheHit.get_value());

    // 4. cache未命中
    ASSERT_EQ(0, cacheMetrics.cacheMiss.get_value());
    cacheMetrics.OnCacheMiss();
    ASSERT_EQ(1, cacheMetrics.cacheMiss.get_value());
}

TEST(CaCheTest, test_cache_with_capacity_limit) {
    int maxCount = 5;
    auto cache = std::make_shared<LRUCache<std::string, std::string>>(maxCount,
        std::make_shared<CacheMetrics>("LruCache"));

    // 1. 测试 put/get
    uint64_t cacheSize = 0;
    for (int i = 1; i <= maxCount + 1; i++) {
        std::string eliminated;
        cache->Put(std::to_string(i), std::to_string(i), &eliminated);
        if (i <= maxCount) {
            cacheSize += std::to_string(i).size() * 2;
            ASSERT_EQ(i, cache->GetCacheMetrics()->cacheCount.get_value());
        } else {
            cacheSize +=
                std::to_string(i).size() * 2 - std::to_string(1).size() * 2;
            ASSERT_EQ(
                cacheSize, cache->GetCacheMetrics()->cacheBytes.get_value());
        }

        std::string res;
        ASSERT_TRUE(cache->Get(std::to_string(i), &res));
        ASSERT_EQ(std::to_string(i), res);
    }

    // 2. 第一个元素被剔出
    std::string res;
    ASSERT_FALSE(cache->Get(std::to_string(1), &res));
    for (int i = 2; i <= maxCount + 1; i++) {
        ASSERT_TRUE(cache->Get(std::to_string(i), &res));
        ASSERT_EQ(std::to_string(i), res);
    }

    // 3. 测试删除元素
    // 删除不存在的元素
    cache->Remove("1");
    // 删除list中存在的元素
    cache->Remove("2");
    ASSERT_FALSE(cache->Get("2", &res));
    cacheSize -= std::to_string(2).size() * 2;
    ASSERT_EQ(maxCount - 1, cache->GetCacheMetrics()->cacheCount.get_value());
    ASSERT_EQ(cacheSize, cache->GetCacheMetrics()->cacheBytes.get_value());

    // 4. 重复put
    std::string eliminated;
    cache->Put("4", "hello", &eliminated);
    ASSERT_TRUE(cache->Get("4", &res));
    ASSERT_EQ("hello", res);
    ASSERT_EQ(maxCount - 1, cache->GetCacheMetrics()->cacheCount.get_value());
    cacheSize -= std::to_string(4).size() * 2;
    cacheSize += std::to_string(4).size() + std::string("hello").size();
    ASSERT_EQ(cacheSize, cache->GetCacheMetrics()->cacheBytes.get_value());
}

TEST(CaCheTest, test_cache_with_capacity_no_limit) {
    auto cache = std::make_shared<LRUCache<std::string, std::string>>(
        std::make_shared<CacheMetrics>("LruCache"));

    // 1. 测试 put/get
    std::string res;
    for (int i = 1; i <= 10; i++) {
        std::string eliminated;
        cache->Put(std::to_string(i), std::to_string(i), &eliminated);
        ASSERT_TRUE(cache->Get(std::to_string(i), &res));
        ASSERT_EQ(std::to_string(i), res);
    }

    // 2. 测试元素删除
    cache->Remove("1");
    ASSERT_FALSE(cache->Get("1", &res));
}

TEST(CaCheTest, TestCacheHitAndMissMetric) {
    auto cache = std::make_shared<LRUCache<std::string, std::string>>(
        std::make_shared<CacheMetrics>("LruCache"));
    ASSERT_EQ(0, cache->GetCacheMetrics()->cacheHit.get_value());
    ASSERT_EQ(0, cache->GetCacheMetrics()->cacheMiss.get_value());

    std::string existKey = "hello";
    std::string notExistKey = "world";
    std::string eliminated;
    cache->Put(existKey, existKey, &eliminated);

    std::string out;
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(cache->Get(existKey, &out));
        ASSERT_FALSE(cache->Get(notExistKey, &out));
    }

    ASSERT_EQ(10, cache->GetCacheMetrics()->cacheHit.get_value());
    ASSERT_EQ(10, cache->GetCacheMetrics()->cacheMiss.get_value());

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(cache->Get(existKey, &out));
    }

    ASSERT_EQ(15, cache->GetCacheMetrics()->cacheHit.get_value());
    ASSERT_EQ(10, cache->GetCacheMetrics()->cacheMiss.get_value());
}

}  // namespace common
}  // namespace curve

