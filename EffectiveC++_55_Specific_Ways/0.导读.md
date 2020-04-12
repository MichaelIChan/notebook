## 术语（Terminology）

初始化（Initialization）是“给予对象初值”的过程。对用户自定义类型的对象而言，初始化由构造函数执行。所谓默认构造函数是一个可被调用而不带任何实参者。这样的构造函数要不没有参数，要不就是每个参数都有缺省值：
```c++
class A {
public:
    A();        // 默认构造函数
};


class B {
public:
    explicit B(int x = 0, bool b = true);   // 默认构造函数
                                            // 关于"explicit"，见以下信息
};

class C {
public:
    explicit C(int x);        // 不是默认构造函数
};
```
上述的类`B`和`C`的构造函数都被声明为`explicit`，这可阻止它们被用来执行隐式类型转换`implicit type conversions`，但它们仍可被用来进行显示类型转换`explicit type conversions`:
```c++
void doSomething(B bObject);    // 函数，接受一个类型为B的对象

B bObj1;                        // 一个类型为B的对象
doSomething(bObj1);             // 没问题，传递一个B给doSomething函数
B bObj2(28);                    // 没问题，根据int 28建立一个B
                                // （函数的boll参数缺省为true）
doSomething(28);                // 错误！doSomething应该接受一个B，
                                // 不是一个int，而int到B之间
                                // 并没有隐式转换
doSomething(B(28));             // 没问题，使用B构造函数将int显示转换
                                //（也就是转型，cast）为一个B以促成此调用
                                // （条款27对转型谈得更多）
```
被声明为`explicit`的构造函数通常比`non-explicit`函数更受欢迎，因为它禁止编译器执行非预期（往往也不期望）的类型转换。

拷贝构造函数被用来“以同型对象初始化自我对象”，拷贝赋值运算符被用来“从另一个同型对象中拷贝其值到自我对象”：
```c++
class Widget {
public:
    Widget();                               // 默认构造函数
    Widget(const Widget& rhs);              // 拷贝构造函数
    Widget& operator=(const Widget& rhs);   // 拷贝赋值运算符
};
Widget w1;          // 调用默认构造函数
Widget w2(w1);      // 调用拷贝构造函数
w1 = w2;            // 调用拷贝赋值运算符
```

当你看到赋值符号时请小心，因为“=”也可用来调用拷贝构造函数：
```
Widget w3 = w2;     // 调用拷贝构造函数
```
如果一个新对象被定义（例如以上语句中的`w3`），一定会有个构造函数被调用，不可能调用赋值操作。如果没有新对象被定义（例如前述的`w1 = w2`语句），就不会有构造函数被调用，当然是赋值操作被调用。

拷贝构造函数定义一个对象如何以值传递`passed by value`：
```c++
bool hasAcceptableQuality(Widget w);
...
Widget aWidget;
if (hasAcceptableQuality(aWidget))...
```
参数`w`是以值传递方式传递给`hasAcceptableQuality`，所以在上述调用中`aWidget`被复制到`w`内。这个复制动作有`Widget`的拷贝构造函数完成。即`Pass-by-value`意味着“调用拷贝构造函数”。但以值传递方式传递用户自定义类型是个坏主意，`Pass-by-reference-to-const`是比较好的选择。