from collections import Counter

# 配置文件名
filename = 'nreap_stats_128_4096.txt'

# try:
with open(filename, 'r') as f:
    line = f.read().strip()  # 读取整行

if not line:
    print("文件为空")
    exit(1)

# 分割字符串并转换为整数
numbers = [int(x) for x in line.split(',') if x.isdigit()]

# 检查是否为 uint8 范围 (0-255)
invalid = [x for x in numbers if x < 0 or x > 255]
if invalid:
    print(f"警告：发现非 uint8 数值（范围外）: {invalid}")

# 统计频次
count = Counter(numbers)

# 输出结果：按数值从小到大排序
print("数值出现次数统计：")
print("-" * 20)
for value in sorted(count.keys()):
    print(f"{value:3d} : {count[value]} 次")

print(f"\n共统计 {len(numbers)} 个元素")
print(f"不重复的数值个数: {len(count)}")

# except FileNotFoundError:
#     print(f"错误：找不到文件 {filename}")
# except ValueError as e:
#     print(f"错误：数据格式不正确，无法解析为整数。{e}")
# except Exception as e:
#     print(f"未知错误：{e}")