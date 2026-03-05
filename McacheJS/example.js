const CacheClient = require("./cacheClient");

async function main() {

    const cache = new CacheClient("127.0.0.1", 1234);

    await cache.connect();

    await cache.set("name", "mahmoud");

    const value = await cache.get("name");
    console.log(value);

    await cache.zadd("scores", 100, "player1");

    const score = await cache.zscore("scores", "player1");
    console.log(score);
}

main();