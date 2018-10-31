/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 *
 * -=- Robust Distributed System Nucleus (rDSN) -=-
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <dsn/dist/fmt_logging.h>
#include <dsn/cpp/message_utils.h>

#include "mutation_batch.h"

namespace dsn {
namespace replication {

/*static*/ constexpr int64_t mutation_batch::PREPARE_LIST_NUM_ENTRIES;

error_s mutation_batch::add(mutation_ptr mu)
{
    if (mu->get_decree() <= _mutation_buffer->last_committed_decree()) {
        // ignore
        return error_s::ok();
    }

    auto old = _mutation_buffer->get_mutation_by_decree(mu->get_decree());
    if (old != nullptr && old->data.header.ballot >= mu->data.header.ballot) {
        // ignore
        return error_s::ok();
    }

    error_code ec = _mutation_buffer->prepare(mu, partition_status::PS_INACTIVE);
    if (ec != ERR_OK) {
        return FMT_ERR(ERR_INVALID_DATA,
                       "mutation_batch: failed to add mutation [err: {}, mutation decree: "
                       "{}, ballot: {}]",
                       ec,
                       mu->get_decree(),
                       mu->get_ballot());
    }

    return error_s::ok();
}

mutation_batch::mutation_batch()
{
    _mutation_buffer =
        make_unique<prepare_list>(0, PREPARE_LIST_NUM_ENTRIES, [this](mutation_ptr &mu) {
            // committer
            add_mutation_if_valid(mu, _loaded_mutations);
        });
}

/*extern*/ void add_mutation_if_valid(mutation_ptr &mu, mutation_tuple_set &mutations)
{
    for (mutation_update &update : mu->data.updates) {
        // ignore WRITE_EMPTY (heartbeat)
        if (update.code == RPC_REPLICATION_WRITE_EMPTY) {
            continue;
        }

        blob bb;
        if (update.data.buffer() != nullptr) {
            bb = std::move(update.data);
        } else {
            bb = blob::create_from_bytes(update.data.data(), update.data.length());
        }

        mutations.emplace(std::make_tuple(mu->data.header.timestamp, update.code, std::move(bb)));
    }
}

} // namespace replication
} // namespace dsn