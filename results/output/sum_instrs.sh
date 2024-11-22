#!/bin/zsh
# 定义一个变量用于存储总和
total_instrs=0
# 定义一个计数器用于统计非零核心的数量
non_zero_core_count=0

# 当前 core 的标识
current_core=""

# 遍历输入文件的每一行
while IFS= read -r line; do
    # 检查是否进入一个 core 块
    if [[ $line == core-* ]]; then
        # 提取当前 core 的标识
        current_core=$(echo $line | awk '{print $1}')
    elif [[ $line == *"instrs:"* ]]; then
        # 提取 instrs 的数值
        instr_value=$(echo $line | awk '{print $2}')
        
        # 如果 instrs 非零，输出该 core 的标识和 instrs 的值
        if (( instr_value != 0 )); then
            echo "$current_core instrs: $instr_value"
            # 增加非零核心计数
            ((non_zero_core_count++))
        fi

        # 将 instrs 值累加到总和
        total_instrs=$((total_instrs + instr_value))
    fi
done < traces.out.zsim.out # 替换为你的输入文件名

# 输出总和和非零核心的数量
echo "Total instrs: $total_instrs"
echo "Non-zero cores count: $non_zero_core_count"
