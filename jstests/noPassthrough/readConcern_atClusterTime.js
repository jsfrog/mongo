// Test parsing of readConcern option 'atClusterTime'.
//
// Only run this test with the WiredTiger storage engine, since we expect other storage engines to
// return early because they do not support snapshot read concern.
// @tags: [requires_wiredtiger, uses_transactions, uses_atclustertime]

function _getClusterTime(rst) {
    const pingRes = assert.commandWorked(rst.getPrimary().adminCommand({ping: 1}));
    assert(pingRes.hasOwnProperty("$clusterTime"), tojson(pingRes));
    assert(pingRes.$clusterTime.hasOwnProperty("clusterTime"), tojson(pingRes));
    return pingRes.$clusterTime.clusterTime;
}

(function() {
"use strict";

// Skip this test if running with --nojournal and WiredTiger.
if (jsTest.options().noJournal &&
    (!jsTest.options().storageEngine || jsTest.options().storageEngine === "wiredTiger")) {
    print("Skipping test because running WiredTiger without journaling isn't a valid" +
          " replica set configuration");
    return;
}

const dbName = "test";
const collName = "coll";

const rst = new ReplSetTest({nodes: 1});
rst.startSet();
rst.initiate();
const testDB = rst.getPrimary().getDB(dbName);

if (!testDB.serverStatus().storageEngine.supportsSnapshotReadConcern) {
    rst.stopSet();
    return;
}

const session = testDB.getMongo().startSession({causalConsistency: false});
const sessionDb = session.getDatabase(dbName);

const clusterTime = _getClusterTime(rst);

// 'atClusterTime' can be used with readConcern level 'snapshot'.
session.startTransaction({readConcern: {level: "snapshot", atClusterTime: clusterTime}});
assert.commandWorked(sessionDb.runCommand({find: collName}));
assert.commandWorked(session.commitTransaction_forTesting());

// 'atClusterTime' cannot be greater than the current cluster time.
const futureClusterTime = new Timestamp(clusterTime.getTime() + 1000, 1);
session.startTransaction({readConcern: {level: "snapshot", atClusterTime: futureClusterTime}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' must have type Timestamp.
session.startTransaction({readConcern: {level: "snapshot", atClusterTime: "bad"}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.TypeMismatch);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot specify a null Timestamp.
session.startTransaction({readConcern: {level: "snapshot", atClusterTime: Timestamp(0, 0)}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot specify a Timestamp with zero seconds.
session.startTransaction({readConcern: {level: "snapshot", atClusterTime: Timestamp(0, 1)}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot be used with readConcern level 'majority'.
session.startTransaction({readConcern: {level: "majority", atClusterTime: clusterTime}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot be used with readConcern level 'local'.
session.startTransaction({readConcern: {level: "local", atClusterTime: clusterTime}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot be used with readConcern level 'available'.
session.startTransaction({readConcern: {level: "available", atClusterTime: clusterTime}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot be used with readConcern level 'linearizable'.
session.startTransaction({readConcern: {level: "linearizable", atClusterTime: clusterTime}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot be used without readConcern level (level is 'local' by default).
session.startTransaction({readConcern: {atClusterTime: clusterTime}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot be used with 'afterOpTime'.
session.startTransaction({
    readConcern:
        {level: "snapshot", atClusterTime: clusterTime, afterOpTime: {ts: Timestamp(1, 2), t: 1}}
});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

// 'atClusterTime' cannot be used outside of a session.
assert.commandFailedWithCode(
    testDB.runCommand(
        {find: collName, readConcern: {level: "snapshot", atClusterTime: clusterTime}}),
    ErrorCodes.InvalidOptions);

// 'atClusterTime' cannot be used with 'afterClusterTime'.
session.startTransaction(
    {readConcern: {level: "snapshot", atClusterTime: clusterTime, afterClusterTime: clusterTime}});
assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
assert.commandFailedWithCode(session.abortTransaction_forTesting(), ErrorCodes.NoSuchTransaction);

session.endSession();
rst.stopSet();

// readConcern with 'atClusterTime' should succeed regardless of value of 'enableTestCommands'.
// So should $_internalReadAtClusterTime.
function testClusterTime() {
    let rst = new ReplSetTest({nodes: 2});
    rst.startSet();
    rst.initiateWithHighElectionTimeout();
    let db = rst.getPrimary().getDB(dbName);
    let secondaryDb = rst.getSecondary().getDB(dbName);
    assert.commandWorked(db.runCommand({create: collName}));
    let session =
        rst.getPrimary().getDB(dbName).getMongo().startSession({causalConsistency: false});
    let sessionDb = session.getDatabase(dbName);
    let clusterTime = _getClusterTime(rst);
    session.startTransaction({readConcern: {level: "snapshot", atClusterTime: clusterTime}});
    assert.commandWorked(sessionDb.runCommand({find: collName}));
    assert.commandWorked(session.commitTransaction_forTesting());
    session.endSession();

    assert.commandWorked(db[collName].insert({_id: 1}));
    rst.awaitReplication();
    let res = assert.commandWorked(
        db.runCommand({find: collName, "$_internalReadAtClusterTime": clusterTime}));
    assert.eq(res.cursor.firstBatch, []);
    let secondaryRes = assert.commandWorked(
        secondaryDb.runCommand({find: collName, "$_internalReadAtClusterTime": clusterTime}));
    assert.eq(secondaryRes.cursor.firstBatch, []);
    // Advance cluster time to see data.
    clusterTime = _getClusterTime(rst);
    res = assert.commandWorked(
        db.runCommand({find: collName, "$_internalReadAtClusterTime": clusterTime}));
    assert.eq(res.cursor.firstBatch, [{_id: 1}]);
    secondaryRes = assert.commandWorked(
        secondaryDb.runCommand({find: collName, "$_internalReadAtClusterTime": clusterTime}));
    assert.eq(secondaryRes.cursor.firstBatch, [{_id: 1}]);

    // Make sure getMore also works
    assert.commandWorked(db[collName].insert([{_id: 2}, {_id: 3}, {_id: 4}, {_id: 5}]));
    rst.awaitReplication();
    clusterTime = _getClusterTime(rst);
    assert.commandWorked(db[collName].insert({_id: 6}));
    rst.awaitReplication();
    res = assert.commandWorked(db.runCommand({
        find: collName,
        batchSize: 3,
        sort: {_id: 1},
        "$_internalReadAtClusterTime": clusterTime
    }));
    secondaryRes = assert.commandWorked(secondaryDb.runCommand({
        find: collName,
        batchSize: 3,
        sort: {_id: 1},
        "$_internalReadAtClusterTime": clusterTime
    }));
    assert.eq(secondaryRes.cursor.firstBatch, [{_id: 1}, {_id: 2}, {_id: 3}]);
    res = assert.commandWorked(
        db.runCommand({getMore: res.cursor.id, collection: collName, batchSize: 3}));
    assert.eq(res.cursor.nextBatch, [{_id: 4}, {_id: 5}]);
    assert.eq(res.cursor.id, 0);
    secondaryRes = assert.commandWorked(secondaryDb.runCommand(
        {getMore: secondaryRes.cursor.id, collection: collName, batchSize: 3}));
    assert.eq(secondaryRes.cursor.nextBatch, [{_id: 4}, {_id: 5}]);
    assert.eq(secondaryRes.cursor.id, 0);

    // Advance the cluster time, now we should see _id: 6 (but not 7 and 8)
    clusterTime = _getClusterTime(rst);
    assert.commandWorked(db[collName].insert([{_id: 7}, {_id: 8}]));
    rst.awaitReplication();
    res = assert.commandWorked(db.runCommand({
        find: collName,
        batchSize: 3,
        sort: {_id: 1},
        "$_internalReadAtClusterTime": clusterTime
    }));
    assert.eq(res.cursor.firstBatch, [{_id: 1}, {_id: 2}, {_id: 3}]);
    secondaryRes = assert.commandWorked(secondaryDb.runCommand({
        find: collName,
        batchSize: 3,
        sort: {_id: 1},
        "$_internalReadAtClusterTime": clusterTime
    }));
    assert.eq(secondaryRes.cursor.firstBatch, [{_id: 1}, {_id: 2}, {_id: 3}]);
    res = assert.commandWorked(
        db.runCommand({getMore: res.cursor.id, collection: collName, batchSize: 5}));
    assert.eq(res.cursor.nextBatch, [{_id: 4}, {_id: 5}, {_id: 6}]);
    assert.eq(res.cursor.id, 0);
    secondaryRes = assert.commandWorked(secondaryDb.runCommand(
        {getMore: secondaryRes.cursor.id, collection: collName, batchSize: 5}));
    assert.eq(secondaryRes.cursor.nextBatch, [{_id: 4}, {_id: 5}, {_id: 6}]);
    assert.eq(secondaryRes.cursor.id, 0);
    rst.stopSet();
}

TestData.enableTestCommands = false;
testClusterTime();
TestData.enableTestCommands = true;
testClusterTime();

// readConcern with 'atClusterTime' is not allowed when enableMajorityReadConcern=false.
{
    let rst = new ReplSetTest({nodes: [{"enableMajorityReadConcern": "false"}]});
    rst.startSet();
    rst.initiate();
    let session =
        rst.getPrimary().getDB(dbName).getMongo().startSession({causalConsistency: false});
    let sessionDb = session.getDatabase(dbName);
    session.startTransaction(
        {readConcern: {level: "snapshot", atClusterTime: _getClusterTime(rst)}});
    assert.commandFailedWithCode(sessionDb.runCommand({find: collName}), ErrorCodes.InvalidOptions);
    assert.commandFailedWithCode(session.abortTransaction_forTesting(),
                                 ErrorCodes.NoSuchTransaction);
    session.endSession();
    rst.stopSet();
}
}());
