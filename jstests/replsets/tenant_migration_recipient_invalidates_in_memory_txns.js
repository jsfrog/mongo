/**
 * Tests that tenant migrations will invalid transactions that are stored in-memory when fetching
 * committed transactions from the donor. We do this by first running a transaction with txnNum 0 on
 * the donor and migrating it to the recipient. After the migration commits, we then run a read-only
 * transaction on the recipient. We expect the read-only transaction to only advance the in-memory
 * state of the transaction -- it should not update the 'config.transaction' entry on the recipient.
 * Then, we run a second migration. By this point, the recipient has an in-memory transaction number
 * of 1 for the session, which is ahead of the donor. However, the migration should still complete,
 * because the recipient will invalidate its in-memory understanding and refetch the on-disk
 * transaction state instead.
 *
 * Note: this test is designed to emulate a back-and-forth migration from donor to recipient,
 * recipient to donor, then donor to recipient again.
 *
 * @tags: [requires_fcv_49, requires_majority_read_concern, incompatible_with_eft,
 * incompatible_with_windows_tls, incompatible_with_macos, requires_persistence]
 */

(function() {
"use strict";

load("jstests/replsets/libs/tenant_migration_test.js");
load("jstests/replsets/rslib.js");
load("jstests/libs/uuid_util.js");

const setParameterOpts = {
    tenantMigrationGarbageCollectionDelayMS: 3 * 1000
};
const tenantMigrationTest =
    new TenantMigrationTest({name: jsTestName(), sharedOptions: {setParameter: setParameterOpts}});
if (!tenantMigrationTest.isFeatureFlagEnabled()) {
    jsTestLog("Skipping test because the tenant migrations feature flag is disabled");
    tenantMigrationTest.stop();
    return;
}

const tenantId = "testTenantId";
const tenantDB = tenantMigrationTest.tenantDB(tenantId, "testDB");
const collName = "testColl";
const transactionsNS = "config.transactions";

const donorPrimary = tenantMigrationTest.getDonorPrimary();
const recipientPrimary = tenantMigrationTest.getRecipientPrimary();

const commitTransaction = (node, lsid, txnNumber, readOnly = false) => {
    txnNumber = NumberLong(txnNumber);

    const cmd = readOnly ? {find: collName} : {insert: collName, documents: [{x: 1}]};
    const cmdObj = Object.assign(cmd, {
        lsid,
        txnNumber,
        stmtId: NumberInt(0),
        startTransaction: true,
        autocommit: false,
    });
    assert.commandWorked(node.getDB(tenantDB).runCommand(cmdObj));

    assert.commandWorked(node.adminCommand({
        commitTransaction: 1,
        lsid,
        txnNumber,
        stmtId: NumberInt(0),
        autocommit: false,
        writeConcern: {w: "majority"}
    }));
};

const session = donorPrimary.startSession({causalConsistency: false});
const lsid = session.getSessionId();

jsTestLog("Committing transaction number 0 on donor");
commitTransaction(donorPrimary, lsid, 0, false /* readOnly */);

jsTestLog("Running the first migration");
const migrationId1 = UUID();
const migrationOpts1 = {
    migrationIdString: extractUUIDFromObject(migrationId1),
    tenantId,
};
let migrationRes = assert.commandWorked(tenantMigrationTest.runMigration(migrationOpts1));
assert.eq(migrationRes.state, TenantMigrationTest.State.kCommitted);
tenantMigrationTest.waitForMigrationGarbageCollection(migrationId1, tenantId);

// Verify that the config.transaction entry was migrated successfully.
let recipientTxnEntry = recipientPrimary.getCollection(transactionsNS).find().toArray()[0];
assert.eq(lsid.id, recipientTxnEntry._id.id);
assert.eq(0, recipientTxnEntry.txnNum);

// Run a read-only txn on the recipient with the same sessionId. The read-only
// transaction should not update the on-disk 'config.transaction' entry, but it will update the
// in-memory txnNum to 1 for the session.
jsTestLog("Committing transaction number 1 on recipient");
commitTransaction(recipientPrimary, lsid, 1, true /* readOnly */);

recipientTxnEntry = recipientPrimary.getCollection(transactionsNS).find().toArray()[0];
assert.eq(lsid.id, recipientTxnEntry._id.id);
// The transaction number should still be 0, since transaction with txnNum 1 was read-only and thus
// will only be updated in memory.
assert.eq(0, recipientTxnEntry.txnNum);

jsTestLog("Dropping tenant DB on recipient");
assert.commandWorked(recipientPrimary.getDB(tenantDB).dropDatabase());

jsTestLog("Running the second migration");
const migrationId2 = UUID();
const migrationOpts2 = {
    migrationIdString: extractUUIDFromObject(migrationId2),
    tenantId,
};
migrationRes = assert.commandWorked(tenantMigrationTest.runMigration(migrationOpts2));
// The migration should have committed successfully even though the in-memory transaction number was
// higher, since the higher number should have been invalidated.
assert.eq(migrationRes.state, TenantMigrationTest.State.kCommitted);

tenantMigrationTest.stop();
})();