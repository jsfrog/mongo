/*
 * Make sure that $gt and $lt queries return the same results regardless of whether there is a
 * multikey index.
 * @tags: [requires_fcv_44]
 */

(function() {
"use strict";
load("jstests/aggregation/extras/utils.js");  // arrayEq
const indexColl = db.indexColl;
const nonIndexedColl = db.nonIndexedColl;
indexColl.drop();
nonIndexedColl.drop();

db.indexColl.createIndex({val: 1});
const collList = [indexColl, nonIndexedColl];

collList.forEach(function(collObj) {
    assert.commandWorked(collObj.insert([
        {val: [1, 2]},
        {val: [3, 4]},
        {val: [3, 1]},
        {val: {"test": 5}},
        {val: [{"test": 7}]},
        {val: [true, false]},
        {val: 2},
        {val: 3},
        {val: 4},
        {val: [2]},
        {val: [3]},
        {val: [4]},
        {val: [1, true]},
        {val: [true, 1]},
        {val: [1, 4]},
        {val: [null]},
        {val: MinKey},
        {val: [MinKey]},
        {val: [MinKey, 3]},
        {val: [3, MinKey]},
        {val: MaxKey},
        {val: [MaxKey]},
        {val: [MaxKey, 3]},
        {val: [3, MaxKey]},
        {val: []},
    ]));
});

const queryList = [
    [2, 2], [0, 3],   [3, 0],      [1, 3],      [3, 1],       [1, 5],   [5, 1],      [1],
    [3],    [5],      {"test": 2}, {"test": 6}, [true, true], [true],   true,        1,
    3,      5,        null,        [null],      [],           [MinKey], [MinKey, 2], [MinKey, 4],
    MinKey, [MaxKey], [MaxKey, 2], [MaxKey, 4], MaxKey,       [],
];

let failedLT = [];
let failedGT = [];

function compareIndexNonIndexedResults(predicate) {
    const query = {val: predicate};
    const projOutId = {_id: 0, val: 1};

    let indexRes = indexColl.find(query, projOutId).sort({val: 1}).toArray();
    let nonIndexedRes = nonIndexedColl.find(query, projOutId).sort({val: 1}).toArray();

    assert(arrayEq(indexRes, nonIndexedRes),
           "Ran query " + tojson(query) + " and got mismatched results.\n Indexed: " +
               tojson(indexRes) + "\n NonIndexed: " + tojson(nonIndexedRes));

    indexRes = indexColl.find(query, projOutId).sort({val: 1}).toArray();
    nonIndexedRes = nonIndexedColl.find(query, projOutId).sort({val: 1}).toArray();
    assert(arrayEq(indexRes, nonIndexedRes),
           "Ran query " + tojson(query) + " and got mismatched results.\n Indexed: " +
               tojson(indexRes) + "\n NonIndexed: " + tojson(nonIndexedRes));
}

queryList.forEach(function(q) {
    const queryPreds = [{"$lt": q}, {"$gt": q}, {$elemMatch: {$not: {$eq: q}}}];
    queryPreds.forEach(compareIndexNonIndexedResults);
});

// We need to run these queries separately.
const inequalityPreds = [{$elemMatch: {$not: {$gte: null}}}, {$elemMatch: {$not: {$lte: null}}}];
inequalityPreds.forEach(compareIndexNonIndexedResults);
})();
