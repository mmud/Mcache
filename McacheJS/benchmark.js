const { MongoClient } = require("mongodb");
const { performance } = require("perf_hooks");

const CacheClient = require("./cacheClient"); 

const ITERATIONS = 10000;
const DATASET_SIZE = 10000;

async function benchmark(name, fn) {

    const start = performance.now();

    for (let i = 0; i < ITERATIONS; i++) {
        await fn(i);
    }

    const end = performance.now();
    const total = end - start;

    console.log(`\n==== ${name} ====`);
    console.log("Total Time:", total.toFixed(2), "ms");
    console.log("Average Latency:", (total / ITERATIONS).toFixed(6), "ms");

    return total;
}

async function main() {

    /* MongoDB setup */

    const mongoClient = new MongoClient("mongodb://127.0.0.1:27017");
    await mongoClient.connect();

    const db = mongoClient.db("benchmark");
    const col = db.collection("users");

    await col.deleteMany({});

    const docs = [];

    for (let i = 0; i < DATASET_SIZE; i++) {
        docs.push({
            id: i,
            name: "user" + i
        });
    }

    await col.insertMany(docs);

    /* Mcache setup */

    const cache = new CacheClient("127.0.0.1", 1234);
    await cache.connect();

    for (let i = 0; i < DATASET_SIZE; i++) {
        await cache.set(
            "user:" + i,
            JSON.stringify({ id: i, name: "user" + i })
        );
    }

    /* Run benchmarks */

    const mongoTime = await benchmark("MongoDB Query", async (i) => {
        await col.findOne({ id: i % DATASET_SIZE });
    });

    const cacheTime = await benchmark("Mcache Query", async (i) => {
        await cache.get("user:" + (i % DATASET_SIZE));
    });

    /* Calculate speedup */

    const speedup = mongoTime / cacheTime;

    console.log("\n============================");
    console.log(`Mcache is ${speedup.toFixed(2)}x faster than MongoDB`);
    console.log("============================\n");

    await mongoClient.close();
}

main();