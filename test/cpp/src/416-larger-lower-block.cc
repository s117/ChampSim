#include <catch.hpp>

#include "cache.h"
#include "defaults.hpp"
#include "mocks.hpp"

SCENARIO("A larger lower-level block can satisfy multiple upper-level MSHRs")
{
  using namespace champsim::data::data_literals;

  GIVEN("A 64B upper cache backed by a 128B lower cache")
  {
    do_nothing_MRC mock_memory{2};
    to_rq_MRP mock_ul;
    champsim::channel upper_to_lower{32, 32, 32, 7_b, false};

    CACHE upper{champsim::cache_builder{champsim::defaults::default_l1d}
                    .name("416-upper")
                    .sets(16)
                    .ways(1)
                    .upper_levels({&mock_ul.queues})
                    .lower_level(&upper_to_lower)
                    .offset_bits(6_b)
                    .mshr_size(8)
                    .hit_latency(1)
                    .fill_latency(1)
                    .tag_bandwidth(champsim::bandwidth::maximum_type{2})};

    CACHE lower{champsim::cache_builder{champsim::defaults::default_l2c}
                    .name("416-lower")
                    .sets(16)
                    .ways(1)
                    .upper_levels({&upper_to_lower})
                    .lower_level(&mock_memory.queues)
                    .offset_bits(7_b)
                    .mshr_size(8)
                    .hit_latency(1)
                    .fill_latency(1)
                    .tag_bandwidth(champsim::bandwidth::maximum_type{2})};

    std::array<champsim::operable*, 4> elements{{&upper, &lower, &mock_memory, &mock_ul}};

    for (auto elem : elements) {
      elem->initialize();
      elem->warmup = false;
      elem->begin_phase();
    }

    WHEN("Two adjacent upper-cache blocks miss within one lower-cache block")
    {
      decltype(mock_ul)::request_type first;
      first.address = champsim::address{0x1000};
      first.cpu = 0;
      first.type = access_type::LOAD;
      first.instr_id = 1;
      first.instr_depend_on_me = {first.instr_id};

      auto second = first;
      second.address = champsim::address{0x1040};
      second.instr_id = 2;
      second.instr_depend_on_me = {second.instr_id};

      REQUIRE(mock_ul.issue(first));
      REQUIRE(mock_ul.issue(second));

      for (uint64_t i = 0; i < 100; ++i) {
        for (auto elem : elements) {
          elem->_operate();
        }
      }

      THEN("The lower cache sends one request to memory") { REQUIRE(mock_memory.packet_count() == 1); }

      THEN("Both upper-cache requests are returned")
      {
        REQUIRE(mock_ul.packets.size() == 2);
        REQUIRE(mock_ul.packets.at(0).return_time > 0);
        REQUIRE(mock_ul.packets.at(1).return_time > 0);
      }
    }
  }
}

SCENARIO("Equal-size lower-level blocks continue to satisfy one upper-level MSHR per returned block")
{
  using namespace champsim::data::data_literals;

  GIVEN("A 64B upper cache backed by a 64B lower cache")
  {
    do_nothing_MRC mock_memory{2};
    to_rq_MRP mock_ul;
    champsim::channel upper_to_lower{32, 32, 32, 6_b, false};

    CACHE upper{champsim::cache_builder{champsim::defaults::default_l1d}
                    .name("416-equal-upper")
                    .sets(16)
                    .ways(1)
                    .upper_levels({&mock_ul.queues})
                    .lower_level(&upper_to_lower)
                    .offset_bits(6_b)
                    .mshr_size(8)
                    .hit_latency(1)
                    .fill_latency(1)
                    .tag_bandwidth(champsim::bandwidth::maximum_type{2})};

    CACHE lower{champsim::cache_builder{champsim::defaults::default_l2c}
                    .name("416-equal-lower")
                    .sets(16)
                    .ways(1)
                    .upper_levels({&upper_to_lower})
                    .lower_level(&mock_memory.queues)
                    .offset_bits(6_b)
                    .mshr_size(8)
                    .hit_latency(1)
                    .fill_latency(1)
                    .tag_bandwidth(champsim::bandwidth::maximum_type{2})};

    std::array<champsim::operable*, 4> elements{{&upper, &lower, &mock_memory, &mock_ul}};

    for (auto elem : elements) {
      elem->initialize();
      elem->warmup = false;
      elem->begin_phase();
    }

    WHEN("Two adjacent upper-cache blocks miss")
    {
      decltype(mock_ul)::request_type first;
      first.address = champsim::address{0x2000};
      first.cpu = 0;
      first.type = access_type::LOAD;
      first.instr_id = 3;
      first.instr_depend_on_me = {first.instr_id};

      auto second = first;
      second.address = champsim::address{0x2040};
      second.instr_id = 4;
      second.instr_depend_on_me = {second.instr_id};

      REQUIRE(mock_ul.issue(first));
      REQUIRE(mock_ul.issue(second));

      for (uint64_t i = 0; i < 100; ++i) {
        for (auto elem : elements) {
          elem->_operate();
        }
      }

      THEN("The lower cache sends one request per upper block to memory") { REQUIRE(mock_memory.packet_count() == 2); }

      THEN("Both upper-cache requests are returned")
      {
        REQUIRE(mock_ul.packets.size() == 2);
        REQUIRE(mock_ul.packets.at(0).return_time > 0);
        REQUIRE(mock_ul.packets.at(1).return_time > 0);
      }
    }
  }
}

SCENARIO("A write-forwarded read does not wake adjacent upper-cache MSHRs")
{
  using namespace champsim::data::data_literals;

  GIVEN("A 64B upper cache with a 128B lower-level channel")
  {
    to_rq_MRP mock_ul;
    champsim::channel upper_to_lower{32, 32, 32, 7_b, false};

    CACHE upper{champsim::cache_builder{champsim::defaults::default_l1d}
                    .name("416-forward-upper")
                    .sets(16)
                    .ways(1)
                    .upper_levels({&mock_ul.queues})
                    .lower_level(&upper_to_lower)
                    .offset_bits(6_b)
                    .mshr_size(8)
                    .hit_latency(1)
                    .fill_latency(1)
                    .tag_bandwidth(champsim::bandwidth::maximum_type{2})};

    std::array<champsim::operable*, 2> elements{{&upper, &mock_ul}};

    for (auto elem : elements) {
      elem->initialize();
      elem->warmup = false;
      elem->begin_phase();
    }

    WHEN("A forwarded read returns while an adjacent block is still waiting")
    {
      decltype(mock_ul)::request_type first;
      first.address = champsim::address{0x3000};
      first.cpu = 0;
      first.type = access_type::LOAD;
      first.instr_id = 5;
      first.instr_depend_on_me = {first.instr_id};

      REQUIRE(mock_ul.issue(first));

      for (uint64_t i = 0; i < 20 && upper.get_mshr_occupancy() != 1; ++i) {
        for (auto elem : elements) {
          elem->_operate();
        }
      }

      REQUIRE(upper.get_mshr_occupancy() == 1);
      REQUIRE(upper_to_lower.RQ.size() == 1);
      upper_to_lower.RQ.clear();

      champsim::channel::request_type write;
      write.address = champsim::address{0x3040};
      write.v_address = write.address;
      write.cpu = 0;
      write.type = access_type::WRITE;
      write.response_requested = false;
      REQUIRE(upper_to_lower.add_wq(write));

      auto second = first;
      second.address = champsim::address{0x3040};
      second.instr_id = 6;
      second.instr_depend_on_me = {second.instr_id};
      REQUIRE(mock_ul.issue(second));

      for (uint64_t i = 0; i < 20 && upper.get_mshr_occupancy() != 2; ++i) {
        for (auto elem : elements) {
          elem->_operate();
        }
      }

      REQUIRE(upper.get_mshr_occupancy() == 2);
      REQUIRE(upper_to_lower.RQ.size() == 1);

      upper_to_lower.check_collision();
      REQUIRE(upper_to_lower.returned.size() == 1);

      for (uint64_t i = 0; i < 20; ++i) {
        for (auto elem : elements) {
          elem->_operate();
        }
      }

      THEN("Only the forwarded read completes")
      {
        REQUIRE(mock_ul.packets.size() == 2);
        REQUIRE(mock_ul.packets.at(0).return_time == 0);
        REQUIRE(mock_ul.packets.at(1).return_time > 0);
        REQUIRE(upper.get_mshr_occupancy() == 1);
      }
    }
  }
}
