这是基于DirectShow的虚拟摄像头驱动源代码工程，
此工程采用从零开发，采用非常原始的方法实现COM基础组件和IBaseFilter，IPin等接口功能。
不依赖 DSHOW的SDK库就可以编译运行， 本代码工程使用VS2015编译。

如果你不喜欢，或者不想去了解DirectShow的工作原理，
大可不必理会我这种比较“疯狂”的做法，也不必下载我的这份代码给你平添无谓的烦恼。

source目录是工程目录，
bin是已经编译好的dll，可以调用register.bat注册，调用unregister.bat注销.
注册成功后，使用DirectShow框架的程序就可以发现并访问这个虚拟摄像头，比如QQ，amcap等。

Fanxiushu 2018

