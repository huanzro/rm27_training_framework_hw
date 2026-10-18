# 开发与验证记录

board
1 在agent 和 培训文档帮助下完成cubemx配置

整体学习思路 ：先让agent完成一份备份工程 再对照学习

libs 
1 先完成lbs/pid 代码编写 目前大致理解其原理 还有部分代码不是很清楚 仍在学习

bsp
1 先完成usart 内容 TODO有相当多部分有agent生成并保留了大部分ai注释 由ai解释代码含义 agent建议在bsp——usart。hpp line37 1.void 改为	改成返回 HAL_StatusTypeDef,让调用者判断 但暂未更改 2.在uint8前加const 暂未加
