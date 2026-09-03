#ifndef USAGE_SECRETS_H
#define USAGE_SECRETS_H

// 智谱开放平台(bigmodel.cn)的 API Key，用于查询 GLM Coding Plan 剩余额度
// 复制本文件为 usage_secrets.h 并填入真实 Key（usage_secrets.h 已被 gitignore）
#define GLM_API_KEY "YOUR_GLM_API_KEY"

// 运行 tools/codex_usage_bridge.py 的电脑在局域网中的地址。
// 留空即可关闭 Codex 查询；OAuth token 只保存在电脑，绝不写进固件。
#define CODEX_BRIDGE_URL ""

// 与桥接程序 --token 的值一致。它只保护局域网额度端点，不是 OpenAI Key。
#define CODEX_BRIDGE_TOKEN ""

#endif
