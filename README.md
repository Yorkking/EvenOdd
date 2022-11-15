# evenodd

## 赛题简介

存储系统保证容错的方式有两种，一种是多副本，即一份数据存储多份；另外一种方式是纠删码。纠删码和多副本相比，可以以更少的存储开销保证容错，因此，可以大大降低存储成本。在保证存储可靠性的同时，怎样在有限的资源下获得最好的编码，解码，和修复性能对存储系统来说十分重要。本次初赛赛题关注一个仅由异或操作便可以完成编解码的、最多可容两错的纠删码——EVENODD。本次赛题的目标是，在保证编解码正确性的基础上，在有限的系统资源下优化 EVENODD 各项操作的性能。具体的赛题说明请参照附件。


## 大文件的 evenodd

- 1. 把文件切分成不同的小块，不同的小块进行 encoding 和 decoding
- 2. 不切分文件，但是目前的 encoding 和 decoding 算法需要修改

## correct_and_time.sh
需要事先编译好生成./evenodd可执行文件

输入参数: file_size prime

file_size：生成测试文件大小，单位为B

prime: 素数

每组数据有5个测试案例，例如：./evenodd 1024 7
1. 删除disk_7和disk_8
2. 删除disk_5和disk_7
3. 删除disk_6和_disk_8
4. 删除disk_5和disk_6
5. 删除disk_5和disk_4

所有测试出错信息被保存在error_log.txt文件里，如果没有error_log.txt说明所有测试没有问题，有error_log.txt文件出现，则说明至少有一种测试数据出错

每一组测试都会在终端显示write和read时间

## run.sh
run.sh是在time_and_correct.sh基础上增加了生成火焰图命令，现在直接运行 run.sh脚本就可以了

执行脚本前，需要在Evenodd主目录下下载火焰图生成工具:

git clone git@github.com:brendangregg/FlameGraph.git

同时运行环境需要提前安装perf工具

例如：sudo ./run.sh 1024 7

执行完毕后，会生成perf图文件夹，里面就是生成的火焰图
