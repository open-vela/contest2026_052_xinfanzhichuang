# VelaClaw 代码提交安全规范

## 🚨 紧急安全事件响应

### API密钥泄露事件 (2026-03-06)
- **事件**: 测试文件中的API密钥被意外提交到Git仓库
- **影响**: OpenAI、Moonshot、Gemini、Qwen API密钥暴露
- **状态**: 🔴 需要立即处理

### 立即行动清单
- [ ] 联系各API提供商安全团队报告泄露
- [ ] 监控API使用记录，查看异常调用
- [ ] 设置API使用量告警和IP限制
- [ ] 联系小米安全团队协助处理
- [ ] 实施下述安全规范防止再次发生

---

## 📋 强制性安全规范

### 1. 提交前检查清单 (MANDATORY)

**每次提交前必须执行：**

```bash
# 1. 检查暂存区文件
git diff --cached --name-only

# 2. 逐个检查每个文件内容
git diff --cached

# 3. 搜索敏感信息
git diff --cached | grep -i -E "(api_key|secret|password|token|credential)"

# 4. 确认没有测试文件
git diff --cached --name-only | grep -E "(test|debug|temp|tmp)"
```

### 2. 禁止的提交内容

**绝对禁止提交：**
- ❌ API密钥、访问令牌
- ❌ 密码、证书、私钥
- ❌ 测试脚本和调试文件
- ❌ 临时文件和日志文件
- ❌ 个人配置和IDE设置
- ❌ 包含真实数据的示例文件

### 3. 安全的提交命令

**✅ 安全做法：**
```bash
# 指定具体文件，避免使用 git add -A
git add src/specific_file.c
git add include/specific_header.h

# 提交前再次确认
git status
git diff --cached

# 提交
git commit -m "具体的提交信息"
```

**❌ 危险做法：**
```bash
git add -A          # 会添加所有文件，包括不应提交的
git add .           # 同样危险
git commit -am      # 跳过检查直接提交
```

---

## 🛡️ 技术防护措施

### 1. 增强的 .gitignore

已更新 `.gitignore` 包含：
```gitignore
# 测试和调试文件
tools/*_test.py
tools/debug_*
tools/temp_*
*_debug.*
*_temp.*

# 敏感信息
*secret*
*credential*
*password*
*.key
*.pem
*.p12
*api_key*
*token*

# 配置文件
.env
.env.*
config.local.*
velaclaw_secrets.h
```

### 2. Pre-commit Hook (推荐安装)

创建 `.git/hooks/pre-commit` 文件：
```bash
#!/bin/bash
# VelaClaw Pre-commit Security Check

echo "🔍 执行安全检查..."

# 检查敏感信息
if git diff --cached | grep -i -E "(sk-[a-zA-Z0-9]{48}|AIza[a-zA-Z0-9]{35}|api_key.*=.*['\"][^'\"]{10,}|secret.*=.*['\"][^'\"]{8,})"; then
    echo "❌ 发现疑似API密钥或敏感信息！"
    echo "请检查并移除敏感信息后重新提交"
    exit 1
fi

# 检查测试文件
if git diff --cached --name-only | grep -E "(test|debug|temp|tmp).*\.(py|js|c|h)$"; then
    echo "❌ 发现测试/调试文件！"
    echo "测试文件不应提交到仓库"
    exit 1
fi

echo "✅ 安全检查通过"
```

### 3. 敏感信息管理

**正确的做法：**
1. 使用 `velaclaw_secrets.h.template` 作为模板
2. 复制为 `velaclaw_secrets.h` 并填入真实密钥
3. `velaclaw_secrets.h` 已在 `.gitignore` 中，不会被提交
4. 运行时通过环境变量或CLI设置密钥

**示例：**
```c
// 在代码中使用
#ifdef VELACLAW_SECRET_API_KEY
    const char *api_key = VELACLAW_SECRET_API_KEY;
#else
    const char *api_key = getenv("VELACLAW_API_KEY");
#endif
```

---

## 🔧 紧急修复工具

### 1. 检查当前仓库敏感信息

```bash
# 搜索历史提交中的敏感信息
git log --all --full-history -- "*.py" | grep -i -E "(api_key|secret|sk-)"

# 搜索当前文件中的敏感信息
find . -type f -name "*.py" -o -name "*.c" -o -name "*.h" | xargs grep -l -i -E "(sk-[a-zA-Z0-9]{20,}|AIza[a-zA-Z0-9]{20,})"
```

### 2. 清理Git历史 (需要管理员权限)

```bash
# 使用 git filter-branch 清理历史
git filter-branch --force --index-filter \
  'git rm --cached --ignore-unmatch tools/llm_tool_latency_test.py tools/stress_test.py' \
  --prune-empty --tag-name-filter cat -- --all

# 强制推送 (需要解除分支保护)
git push --force origin main
```

---

## 📚 培训和流程

### 1. 开发者培训要点

- **意识培养**: 敏感信息泄露的严重后果
- **技能培训**: 安全的Git操作方法
- **工具使用**: Pre-commit hook和检查脚本
- **应急响应**: 发现泄露后的处理流程

### 2. 代码审查要求

- 所有提交必须经过代码审查
- 审查者必须检查敏感信息
- 使用自动化工具辅助审查
- 建立审查清单和标准

### 3. 定期安全审计

- 每月检查Git历史中的敏感信息
- 审查 `.gitignore` 文件的有效性
- 更新安全规范和工具
- 进行安全意识培训

---

## 🚨 事件报告流程

### 发现敏感信息泄露时：

1. **立即停止** - 不要继续推送代码
2. **评估影响** - 确定泄露的信息类型和范围
3. **通知团队** - 立即通知项目负责人和安全团队
4. **采取行动** - 轮换密钥、限制访问、清理历史
5. **事后分析** - 分析原因、改进流程、防止再次发生

### 联系方式：
- 项目负责人: [待填写]
- 安全团队: [待填写]
- 紧急联系: [待填写]

---

## ✅ 合规检查清单

提交前请确认：

- [ ] 没有使用 `git add -A` 或 `git add .`
- [ ] 已逐个检查每个暂存文件
- [ ] 搜索并确认没有敏感信息
- [ ] 没有包含测试、调试、临时文件
- [ ] 提交信息清晰描述了更改内容
- [ ] 如有疑问，已咨询团队成员

**记住：安全是每个人的责任！**