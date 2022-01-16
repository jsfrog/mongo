# 基础知识
主要聊聊基本的一些用法及一些基本名词的解释，如果对mongo了解比较多的可以跳过了。如果需要详细了解用法可以看官方文档，这里只是一笔带过。

# 名称解释
- db: database 数据库
- collection: 集合或者表
- document: 文档，也就是mongo存储的数据
- explain: 执行计划
- _id: 每个document的主键，如果创建的时候不指定_id则会自动创建
- aggregation: 聚合查询 
- bson: MongoDB 中存储文档和进行远程过程调用的序列化格式
- index: 索引
- driver: 数据库驱动
- field: document中的字段
- lock: 锁
- mongo: mongodb shell 
- mongod: mongodb server
- mongos: mongodb 分片 路由
- namespace: 命名空间 [database-name].[collection-or-index-name]
- operator: 操作符 以$开头
- pipeline: 管道 聚合过程中的一系列操作
- query optimizer: 查询优化器 用于生成查询计划
- query shape： 查询模型 predicate、sort、projection的组合
- solution： 解决方案


> 假设你已经使用mongo shell连接上了

# 查询命令
```shell
# 查看存在的db列表
show dbs
# 切换db
use doc
# 查看当前db已经存在的collection
show collections
```

# curd
> 需要先切换到具体的db下操作
```shell
# 新建
db.test.insert({name: "mongo"});
# 更新
db.test.updateOne({name: "mongo"}, {$set: {name: "mongodb"}});
# 删除
db.test.remove({name: "mongo"});
# find查询
db.test.findOne({name: "mongo"});
# aggregate查询
db.test.aggregate([{$match: {name: "mongodb"}}]);
```

# index
```shell
# 创建索引
db.test.createIndex({name: 1})
# 查看索引
db.test.getIndexes()
# 重建索引
db.test.reIndex()
# 查看索引大小
db.test.totalIndexSize()
# 删除索引
db.test.dropIndex("name_1")
```

# plan cache
```shell
# 查看执行计划缓存
db.test.getPlanCache().list()
# 清除执行计划缓存
db.test.getPlanCache().clear()
```

# profile
```shell
# 查询最新的一条的日志
db.system.profile.find().sort({ts: -1}).limit(1)
```

# explain
```shell
# find 命令查看执行计划
db.test.find({name: "mongo"}).explain(true);
# aggregate 命令查看执行计划
db.test.explain("queryPlanner").aggregate([{$match: {name: "mongodb"}}]);
```


