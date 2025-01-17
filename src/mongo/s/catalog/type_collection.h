/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <boost/optional.hpp>
#include <string>

#include "mongo/db/jsobj.h"
#include "mongo/db/keypattern.h"
#include "mongo/db/namespace_string.h"
#include "mongo/util/uuid.h"

namespace mongo {

class Status;
template <typename T>
class StatusWith;


/**
 * This class represents the layout and contents of documents contained in the config server's
 * config.collections collection. All manipulation of documents coming from that collection
 * should be done with this class.
 *
 * Expected config server config.collections collection format:
 *   {
 *      "_id" : "foo.bar",
 *      "lastmodEpoch" : ObjectId("58b6fd76132358839e409e47"),
 *      "lastmod" : ISODate("1970-02-19T17:02:47.296Z"),
 *      "dropped" : false,
 *      "key" : {
 *          "_id" : 1
 *      },
 *      "defaultCollation" : {
 *          "locale" : "fr_CA"
 *      },
 *      "unique" : false,
 *      "uuid" : UUID,
 *      "noBalance" : false,
 *      "distributionMode" : "unsharded|sharded",
 *      "permitMigrations": false
 *   }
 *
 */
class CollectionType {
public:
    // Name of the collections collection in the config server.
    static const NamespaceString ConfigNS;

    static const BSONField<std::string> fullNs;
    static const BSONField<OID> epoch;
    static const BSONField<Date_t> updatedAt;
    static const BSONField<BSONObj> keyPattern;
    static const BSONField<BSONObj> defaultCollation;
    static const BSONField<bool> unique;
    static const BSONField<UUID> uuid;
    static const BSONField<std::string> distributionMode;
    static const BSONField<bool> permitMigrations;

    /**
     * Constructs a new CollectionType object from BSON. Also does validation of the contents.
     *
     * Dropped collections accumulate in the collections list, through 3.6, so that
     * mongos <= 3.4.x, when it retrieves the list from the config server, can delete its
     * cache entries for dropped collections.  See SERVER-27475, SERVER-27474
     */
    static StatusWith<CollectionType> fromBSON(const BSONObj& source);

    enum class DistributionMode {
        kUnsharded,
        kSharded,
    };

    /**
     * Returns OK if all fields have been set. Otherwise returns NoSuchKey and information
     * about what is the first field which is missing.
     */
    Status validate() const;

    /**
     * Returns the BSON representation of the entry.
     */
    BSONObj toBSON() const;

    /**
     * Returns a std::string representation of the current internal state.
     */
    std::string toString() const;

    const NamespaceString& getNs() const {
        return _fullNs.get();
    }
    void setNs(const NamespaceString& fullNs);

    OID getEpoch() const {
        return _epoch.get();
    }
    void setEpoch(OID epoch);

    Date_t getUpdatedAt() const {
        return _updatedAt.get();
    }
    void setUpdatedAt(Date_t updatedAt);

    bool getDropped() const {
        return _dropped.get_value_or(false);
    }
    void setDropped(bool dropped) {
        _dropped = dropped;
    }

    const KeyPattern& getKeyPattern() const {
        return _keyPattern.get();
    }
    void setKeyPattern(const KeyPattern& keyPattern);

    const BSONObj& getDefaultCollation() const {
        return _defaultCollation;
    }
    void setDefaultCollation(const BSONObj& collation) {
        _defaultCollation = collation.getOwned();
    }

    bool getUnique() const {
        return _unique.get_value_or(false);
    }
    void setUnique(bool unique) {
        _unique = unique;
    }

    boost::optional<UUID> getUUID() const {
        return _uuid;
    }

    void setUUID(UUID uuid) {
        _uuid = uuid;
    }

    bool getAllowBalance() const {
        return _allowBalance.get_value_or(true);
    }

    void setPermitMigrations(bool permit) {
        if (permit) {
            _permitMigrations = boost::none;
        } else {
            _permitMigrations = permit;
        }
    }

    bool getPermitMigrations() const {
        return _permitMigrations.get_value_or(true);
    }

    void setDistributionMode(DistributionMode distributionMode) {
        _distributionMode = distributionMode;
    }

    DistributionMode getDistributionMode() const {
        return _distributionMode.get_value_or(DistributionMode::kSharded);
    }

    bool hasSameOptions(const CollectionType& other) const;

private:
    // Required full namespace (with the database prefix).
    boost::optional<NamespaceString> _fullNs;

    // Required to disambiguate collection namespace incarnations.
    boost::optional<OID> _epoch;

    // Required last updated time.
    boost::optional<Date_t> _updatedAt;

    // New field in v4.4; optional in v4.4 for backwards compatibility with v4.2. Whether the
    // collection is unsharded or sharded. If missing, implies sharded.
    boost::optional<DistributionMode> _distributionMode;

    // Optional, whether the collection has been dropped. If missing, implies false.
    boost::optional<bool> _dropped;

    // Sharding key. Required, if collection is not dropped.
    boost::optional<KeyPattern> _keyPattern;

    // Optional collection default collation. If empty, implies simple collation.
    BSONObj _defaultCollation;

    // Optional uniqueness of the sharding key. If missing, implies false.
    boost::optional<bool> _unique;

    // Optional in 3.6 binaries, because UUID does not exist in featureCompatibilityVersion=3.4.
    boost::optional<UUID> _uuid;

    // Optional whether balancing is allowed for this collection. If missing, implies true.
    boost::optional<bool> _allowBalance;

    // Optional whether migration is allowed for this collection. If missing, implies true.
    boost::optional<bool> _permitMigrations;
};

}  // namespace mongo
