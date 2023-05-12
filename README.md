# hpm_rv32ima
​	参考 [CNLohr's mini-rv32ima](https://github.com/cnlohr/mini-rv32ima) 以及 [uc-rv32ima](https://github.com/xhackerustc/uc-rv32ima.git)  感谢大佬开源。

## Why

 - 验证hpmicro运行Linux的可能性
 - 也仅仅只是为了好玩

## How it works

- 使用该仓库hpm_rv32ima文件夹拷贝到hpm_sdk中的samples，再使用start_gui.exe进行构建工程(不清楚使用的建议查看先楫官方公众号教程)。支持hpm6750evkmini, hpm6750evk,hpm6750evk2,hpm6300evk
- 准备好一张卡，如果不是FAT32，请进行格式化。然后把image里面的所有文件放在SD卡根目录
- 编译烧录，然后enjoy。

## Running the example

使用虚拟机启动linux，测试hpm6750evk2，启动时间大概3S左右。对于直接运行nommu linux还是有一定的可能性。

https://user-images.githubusercontent.com/14258359/237963650-acb77c44-9ad0-43cb-9cfa-11e66c10309a.mp4