# C++ Style Guide

## 目录

1. [背景](#背景)
2. [头文件](#头文件)
3. [作用域](#作用域)
4. [类](#类)
5. [函数](#函数)
6. [其他 C++ 特性](#其他-c-特性)
7. [命名约定](#命名约定)
8. [注释](#注释)
9. [格式](#格式)
10. [规则例外](#规则例外)

---

## 背景

C++ 是 Google 使用的主要开发语言之一。本指南参考Google编码规范旨在通过详细阐述 C++ 编码规范来管理 C++ 代码的复杂性，使代码更易于管理，同时保持高效。

### 目标

- 提高代码的可读性和可维护性
- 减少代码中的错误
- 促进团队协作
- 确保代码风格的一致性

---

## 头文件

### 自包含头文件

头文件应该是自包含的（self-contained），即可以独立编译。

```cpp
// foo.h
#ifndef FOO_H_
#define FOO_H_

// 头文件内容

#endif  // FOO_H_
```

### #define 保护

所有头文件都应该使用 `#define` 保护来防止多重包含。保护宏的命名格式为：`<PROJECT>_<PATH>_<FILE>_H_`

```cpp
// myproject/src/base/logging.h
#ifndef MYPROJECT_SRC_BASE_LOGGING_H_
#define MYPROJECT_SRC_BASE_LOGGING_H_

// ...

#endif  // MYPROJECT_SRC_BASE_LOGGING_H_
```

### 前置声明

与现行 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) 一致：**尽量避免前置声明，优先 `#include` 所需头文件**（Include What You Use）。

前置声明可能隐藏依赖、妨碍工具分析，并在库升级时悄悄改变重载解析结果。**不要**前置声明本项目不拥有的类型（尤其禁止对 `std::` 中的实体做前置声明）。

仅在同时满足下列条件时，可对**本项目拥有**的类型使用前置声明：

- 完整头文件包含成本过高，且短期无法把该头做轻；
- 工程判断前置声明明显优于反复重编。

```cpp
// 推荐：直接包含
#include "path/to/my_class.h"

void Func(const MyClass& obj);

// 仅在有充分理由时，对本项目类型前置声明：
// class MyClass;
// void Func(const MyClass& obj);
```

### Include What You Use（用到即包含）

每个文件应**直接** `#include` 它所使用的每个符号的头文件，不要依赖「别的头已经间接包含了」。

这样别人从公共头里删掉不再需要的 include 时，不会悄悄弄坏你的翻译单元；也符合工具（IWYU、clangd）的分析方式。

```cpp
// foo.cc 用到了 Bar，即使 foo.h 已包含 bar.h，foo.cc 仍应自行包含
#include "foo/foo.h"

#include "bar/bar.h"

void Foo::UseBar(const Bar& bar) { /* ... */ }
```

相关头文件仍应放在 include 列表的最前面（见「包含顺序」），以便相关头自身缺少 include 时，优先在本文件编译中暴露问题。

### 内联函数

只有当函数体很小（通常不超过 10 行）且频繁调用时，才使用内联函数。

```cpp
inline int Add(int a, int b) { return a + b; }
```

### 包含顺序

使用标准顺序来增强可读性：相关头文件、C 库、C++ 库、其他库的头文件、本项目头文件。

```cpp
#include "foo/server/bar.h"  // 优先位置

#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/commandlineflags.h"
#include "foo/server/baz.h"
```

---

## 作用域

### 命名空间

鼓励在 `.cc` 文件中使用匿名命名空间或 `static` 来限定作用域。避免使用 `using` 指令。

```cpp
// .cc 文件中
namespace {

void InternalFunction() {
  // ...
}

}  // namespace
```

### 嵌套类

当嵌套类是接口的一部分时，可以将其声明为公有；否则应声明为私有。

```cpp
class Outer {
 public:
  class Inner {
   public:
    void Method();
  };

 private:
  class PrivateInner {
    // ...
  };
};
```

### 非成员函数、静态成员函数和全局函数

优先使用非成员函数或静态成员函数，而不是全局函数。

```cpp
// 推荐
class StringUtility {
 public:
  static bool StartsWith(const std::string& str, const std::string& prefix);
};

// 不推荐
bool StartsWith(const std::string& str, const std::string& prefix);
```

### 局部变量

将局部变量声明放在尽可能小的作用域内，并尽可能靠近首次使用的位置。

```cpp
for (int i = 0; i < kMaxSize; ++i) {
  int value = array[i];
  // 使用 value
}
```

### 静态和全局变量

禁止定义静态存储期的非 POD（Plain Old Data）类类型变量。

```cpp
// 允许（常量命名见下文「常量命名」：k + 混合大小写）
static const char kGreeting[] = "Hello";
static const int kMagicNumber = 42;

// 禁止
static std::string g_name;  // 非平凡析构的静态对象，构造/析构顺序未定义
```

---

## 类

### 构造函数的职责

构造函数可以完成对象所需的初始化，使构造结束后对象可用。须遵守：

- **禁止在构造函数中调用虚函数**（不会分派到子类实现）。
- 初始化若可能失败，且不能用「终止进程」表达错误时：优先**工厂函数**（返回 `unique_ptr` / 空表示失败），或谨慎使用 `Init()`。
- 避免长期处于「半构造」状态却几乎每个方法都要先查 `IsValid()` 的对象；若使用 `Init()`，须在文档中写清失败后哪些接口可调用。

```cpp
// 简单、不会失败：构造完成即可用
class MyClass {
 public:
  explicit MyClass(int size) : size_(size) {}

 private:
  int size_;
};

// 可能失败：工厂优于半初始化对象
class Parser {
 public:
  static std::unique_ptr<Parser> Create(const Options& opt);
  // 禁止对外暴露「默认构造 + 易忘的 Init」却无状态约定
};
```

### 显式构造函数

对单参数构造函数使用 `explicit` 关键字，避免隐式类型转换。

```cpp
class Foo {
 public:
  explicit Foo(int size);
  explicit Foo(const std::string& name);
};
```

### 拷贝与移动

类的公开 API 须让人一眼看出：可拷贝、仅可移动、或既不可拷贝也不可移动。仅在语义清晰时支持拷贝/移动。

```cpp
// 禁用拷贝与移动
class NonCopyable {
 public:
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;
  NonCopyable(NonCopyable&&) = delete;
  NonCopyable& operator=(NonCopyable&&) = delete;
};

// 仅可移动（例如独占资源）
class MoveOnly {
 public:
  MoveOnly(MoveOnly&&) noexcept;
  MoveOnly& operator=(MoveOnly&&) noexcept;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
};
```

管理资源时遵循三/五法则：需要自定义析构、拷贝或移动时，显式处理其余特殊成员，或 `= default` / `= delete` 写清楚意图。

### 结构体 vs 类

仅当只有数据成员时使用 `struct`，否则使用 `class`。

```cpp
// 仅包含数据
struct Point {
  int x;
  int y;
};

// 包含方法
class Rectangle {
 public:
  int Area() const;
 private:
  Point top_left_;
  Point bottom_right_;
};
```

### 继承

优先使用组合而不是继承。继承应使用 `public` 继承。析构函数应为 `virtual` 或 `protected`。

```cpp
// 基类
class Base {
 public:
  virtual ~Base() = default;
  virtual void DoSomething() = 0;
};

// 派生类
class Derived : public Base {
 public:
  void DoSomething() override;
};
```

### 多重继承

只允许一个非接口类的多重继承。

```cpp
// 允许：一个实现类 + 多个纯接口类
class MyClass : public BaseClass, public Interface1, public Interface2 {
  // ...
};
```

### 接口

接口类是指只有纯虚函数和静态方法的类。

```cpp
class Process {
 public:
  virtual ~Process() {}
  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual int GetExitCode() = 0;
};
```

### 操作符重载

谨慎使用操作符重载，只在有明确语义的情况下使用。

```cpp
class BigInteger {
 public:
  BigInteger operator+(const BigInteger& other) const;
  bool operator==(const BigInteger& other) const;
};
```

### 访问控制

类成员应按 `public:`、`protected:`、`private:` 的顺序声明，每个访问修饰符缩进一个空格。

```cpp
class MyClass {
 public:
  MyClass();
  void PublicMethod();

 protected:
  void ProtectedMethod();

 private:
  void PrivateMethod();
  int private_member_;
};
```

### 声明顺序

类中的声明顺序：

1. `typedef` 和枚举
2. 常量
3. 构造函数
4. 析构函数
5. 方法（包括静态方法）
6. 数据成员（包括静态数据成员）

---

## 函数

### 参数顺序

输入参数在前，输出参数在后。

```cpp
void ProcessData(const InputData& input, OutputData* output);
```

### 引用参数

所有按引用传递的参数必须加上 `const`，除非它们是用来输出值的。

```cpp
void Print(const std::string& message);  // 输入参数
void Parse(const std::string& input, int* value);  // 输入 + 输出
```

### 函数重载

仅在可读性获得明显提升时才使用函数重载。

```cpp
// 不推荐
void OpenFile(const std::string& filename);
void OpenFile(const std::string& filename, int mode);

// 推荐
void OpenFileForRead(const std::string& filename);
void OpenFileForWrite(const std::string& filename, int mode);
```

### 默认参数

默认参数只用在非虚函数上。

```cpp
// 允许
void Print(const std::string& message, int level = 0);

// 禁止（虚函数）
virtual void Process(int value = 0);  // 避免
```

### 函数长度

函数应该简短而凝练。如果函数超过 40 行，考虑将其分解。

### 返回值

不要省略返回值。使用列表初始化返回简单值。

```cpp
// 推荐
std::vector<int> GetNumbers() {
  return {1, 2, 3};
}
```

---

## 其他 C++ 特性

### 异常与错误处理

**不使用 C++ 异常**（不要 `throw` / `try` / `catch` 作为常规控制流）。

理由简述：异常会让控制流难以从调用点看清，并要求整条调用链具备异常安全；在大型代码库中成本通常高于收益。

失败与错误应通过以下方式之一表达（按场景选择，并在接口上保持一致）：

- 返回 `bool` / 错误码，输出参数用指针写出结果；
- 返回可空或带状态的值（如 `optional`、带 `ok()` 的状态类型，若工程已采用）；
- 构造可能失败时，优先**工厂函数**返回空指针 / 错误状态，避免半初始化对象；需要两段初始化时可用 `Init()`，但必须明确失败后对象不可用或可安全销毁。

```cpp
// 推荐：失败可预期时返回状态，不抛异常
bool LoadConfig(const std::string& path, Config* out);

// 推荐：所有权 + 失败
std::unique_ptr<Parser> Parser::Create(const Options& opt);

// 禁止：用异常表达普通业务失败
// Config LoadConfigOrThrow(const std::string& path);
```

第三方库若抛异常，应在边界捕获并转换为项目内的错误表示，避免异常穿过项目核心代码。

### 所有权与智能指针

动态分配的对象应有**明确、固定的所有者**。所有权转移优先用智能指针表达，避免裸 `new`/`delete` 散落各处。

| 类型 | 用途 |
|------|------|
| `std::unique_ptr` | **独占**所有权；转移用 `std::move` |
| `std::shared_ptr` | **共享**所有权；仅在确有必要时使用（如只读共享、避免昂贵拷贝且对象不可变） |
| 裸指针 / 引用 | **不拥有**，仅观察；调用方保证生命周期 |

```cpp
// 工厂交出独占所有权
std::unique_ptr<Foo> CreateFoo();

// 接收方接管所有权
void ConsumeFoo(std::unique_ptr<Foo> foo);

// 不转移所有权：传引用或观察指针
void ObserveFoo(const Foo& foo);
```

不要设计默认「到处 `shared_ptr`」。禁止使用已废弃的 `std::auto_ptr`。能用值语义或不分配堆时，优先不用智能指针。

### 强制转换

**禁止 C 风格强转**（如 `(int)x`），`void` 转换等极少数情况除外。需要显式转换时使用 C++ 风格：

- 算术类型优先列表初始化：`int64_t{x}`（可能丢信息时更易在编译期暴露）；
- 一般值转换 / 上下转型：`static_cast`；
- 去掉 `const`：`const_cast`（尽量少用）；
- 底层位型别转换：`reinterpret_cast` 或等价安全工具（须清楚别名规则）。

```cpp
// 禁止
int n = (int)value;

// 推荐
int n = static_cast<int>(value);
int64_t big = int64_t{small};
```

不要用强制转换掩盖设计问题；优先修正类型与接口。

### `nullptr`

指针空值使用 **`nullptr`**，不要用 `0` 或 `NULL`。字符空值使用 `'\0'`，不要用字面量 `0` 表示「空字符」。

```cpp
Foo* p = nullptr;
if (p == nullptr) {
  return false;
}

char end = '\0';
```

### `auto` 与类型推导

仅在**提高对陌生读者的可读性或提高安全性**时使用类型推导，不要只为少打字而写 `auto`。

适合：

- 迭代器、冗长的模板类型，类型在右侧初始化中已足够明显；
- 与 `static_cast` 等搭配，避免重复写类型。

不适合：

- 类型对理解代码很关键，但 `auto` 会把它藏起来；
- 仅因懒得写 `std::string`、`int` 等短类型。

```cpp
// 尚可：类型在右侧清楚
auto it = table.find(key);

// 不推荐：读者看不出是什么
auto result = DoSomething();

// 推荐：关键返回值写明类型
Status result = DoSomething();
```

函数返回类型、接口签名优先写明类型；局部变量才考虑 `auto`。

### 整数类型

内置整数类型中，日常优先使用 **`int`**（假定至少 32 位，但不假定不止 32 位）。需要固定宽度时，使用 `<cstdint>` / `<stdint.h>` 中的类型，例如 `int16_t`、`int32_t`、`int64_t`。

- 可能 ≥ 2³¹ 或中间计算会放大的值：用 `int64_t`；
- 容器大小等：可用 `size_t`、`ptrdiff_t` 等标准别名；
- **不要**仅因为「值不会为负」就改用无符号类型；无符号优先用于位模式、模 2ⁿ 算术。混用有符号/无符号容易引入难查的 bug。

```cpp
int count = 0;           // 普通计数、循环
int64_t file_size = 0;   // 可能很大
uint32_t flags = 0;      // 位标志
```

转换整数类型时要谨慎，窄化与有符号/无符号混用可能导致未定义行为或安全问题。

### `const` 与 `constexpr`

API 中在有意义且准确的地方使用 `const`（参数、成员函数、非局部变量），便于区分读/写，也有利于并发推理。

- 不会修改的引用/指针参数：用 `const T&` / `const T*`；
- 不改变对象逻辑状态的成员函数：声明为 `const`；同一对象的多个 `const` 操作应可安全并发调用（否则须文档说明非线程安全）；
- 按值传递的参数不必加 `const`（对调用方无影响）；
- 局部变量是否加 `const`：不作强制，按可读性选择。

真正的编译期常量优先用 `constexpr`（含支持其定义的函数/构造）。不要为了「强迫内联」而滥用 `constexpr`。

```cpp
void Print(const std::string& msg);

class Table {
 public:
  int size() const { return size_; }
  void clear();

 private:
  int size_ = 0;
};

constexpr int kMaxRetries = 3;
```

`const` 书写习惯推荐 `const int*` 形式，与周围代码保持一致即可。

### 右值引用与 `std::move`

**默认不要**在接口里随意使用右值引用（`T&&`）或给成员函数加 `&&` 限定。仅允许：

- 定义**移动构造** / **移动赋值**；
- 成员函数以 `&&` 限定表示「消费 `*this`」，调用后对象处于空/不可用状态；
- 与 `std::forward` 一起做**完美转发**（转发引用）；
- 成对重载 `const T&` / `T&&`，且有可测的性能收益；否则优先按值传递。

```cpp
// 允许：移动
Foo(Foo&& other) noexcept;
Foo& operator=(Foo&& other) noexcept;

// 允许：完美转发
template <typename T>
void Emplace(T&& value) {
  data_.push_back(std::forward<T>(value));
}

// 不推荐：普通参数仅为「看起来现代」而写成 T&&
// void SetName(std::string&& name);
```

需要把左值当成可移动资源交出时，使用 `std::move`，且仅在移动后不再使用该对象（或只处于明确的空状态）。

### Lambda（使用规则）

适合作为短小的函数对象传给算法或回调。格式见「格式 → Lambda 表达式」。

- Lambda **可能逃出当前作用域**（异步、存入容器、延长生命周期）时：优先**显式列出捕获**，避免默认捕获 `[&]` / `[=]` 掩盖悬空风险；
- 默认按值捕获**不能**防止悬空：按值捕获指针/`this` 只拷贝指针，对象仍可能已销毁；
- 慎用隐式捕获 `this`（通过成员访问）；逃逸场景写明 `this` 或改传所需数据；
- 过长、多层嵌套的匿名 Lambda 应改成命名函数或函数对象。

```cpp
// 不推荐：异步任务默认 [&]，foo / this 可能已销毁
executor->Schedule([&] { Frobnicate(foo); });

// 推荐：显式捕获，意图清楚
executor->Schedule([&foo] { Frobnicate(foo); });

// 局部、不逃逸：短 Lambda + 默认捕获尚可
std::sort(v.begin(), v.end(), [&](int a, int b) {
  return weight[a] < weight[b];
});
```

### RTTI

**避免**使用运行时类型信息（`typeid`、`dynamic_cast` 做业务分支）。需要按子类型走不同逻辑时，优先：

- 虚函数（行为放在对象自身）；
- 或 Visitor 等双分派，把类型决策留在类型系统内。

允许的例外：单元测试（如校验工厂产物类型）；逻辑上已保证基类指针实际指向某派生类时的向下转型（仍须谨慎）。

```cpp
// 不推荐：类型决策树，加子类就易漏改
if (typeid(*p) == typeid(A)) {
  ...
} else if (typeid(*p) == typeid(B)) {
  ...
}

// 推荐：虚函数
p->DoWork();
```

不要手写「类型标签 + switch」去绕过 RTTI，问题本质相同。

### 慎用 / 禁用的标准库能力

下列能力易损害可读性、可测性或可移植性，**不要使用**（与 Google 现行指南对齐，团队可再增补）：

- `<ratio>`（编译期有理数，易导向过重的模板风格）；
- `<cfenv>` / `<fenv.h>`（多编译器支持不可靠）；
- `<filesystem>`（测试与安全面不足，且易踩坑）。

需要文件路径/遍历时，使用项目已统一的文件/路径库，或经评审后再引入替代方案。其他标准库能力默认可用，但若与「可读性 / 与周边一致」冲突，优先跟项目既有用法。

---

## 命名约定

### 通用命名规则

命名应该具有描述性，避免缩写，除非缩写是广为人知的。

### 类型命名

类型名称的每个单词首字母均大写，不包含下划线。

```cpp
// 类和结构体
class UrlTable { ... };
class UrlTableTester { ... };
struct UrlTableProperties { ... };

// 类型别名
using PropertiesTable = std::vector<UrlTableProperties>;
```

### 函数中 `{}` 使用规则

**与 Google 格式对齐的默认写法（推荐）**：函数定义的 `{` 与签名同一风格放在行末（不单独占一行成 Allman 风格）；`if` / `else` / `for` 等控制语句即使只有一行语句也必须加大括号。

```cpp
void GetNameByIndex() {
  if (xx) {
    // ...
  } else if (yy) {
    // ...
  } else {
    return;
  }
}
```

**团队例外（非 Google 原文）**：若某子模块历史代码统一采用「函数体 `{` 独占一行」，修改该模块时允许与周围代码保持一致；**新文件、新模块仍按上面的 Google 风格**。控制语句「单行也必须 `{}`」两条路径均适用，不作例外。

### 变量命名

变量名一律小写，单词之间用下划线连接。

```cpp
std::string table_name_;  // 类成员变量（以_结尾）
std::string full_table_name;  // 局部变量（无后缀）
```

### 类数据成员

类的数据成员（静态变量除外）以尾下划线 `_` 结尾。

```cpp
class MyClass {
 private:
  std::string private_member_;
  static int static_member;
};
```

### 结构体变量

结构体的数据成员不使用下划线后缀。

```cpp
struct UrlTableProperties {
  std::string name;
  int num_entries;
};
```

### 全局变量命名

全局变量（包括命名空间作用域的变量）以 `g_` 开头，后面使用**驼峰命名法**（PascalCase）。

```cpp
// 全局变量
g_ConnectionCount = 0;
g_MaxBufferSize = 1024;
std::string g_ConfigFilePath;

// 命名空间内的全局变量
namespace config {
int g_TimeoutSeconds = 30;
std::string g_VersionString = "1.0.0";
}  // namespace config
```

**注意**：应尽量避免使用全局变量，优先使用单例模式或依赖注入。

### 常量命名

程序生命周期内固定的 `const` / `constexpr` 变量：以 `k` 开头，后接混合大小写（**不是** `k_` 下划线前缀）。仅在大小写无法分词的罕见情况才用下划线分隔片段。

```cpp
const int kDaysInAWeek = 7;
const char kCompanyName[] = "Google";
const int kAndroid8_0_0 = 24;  // 版本号等无法用大小写切开时
```

### 函数命名

#### 普通函数

普通函数（全局函数、命名空间内的函数）使用**驼峰命名法**（PascalCase），即每个单词首字母大写，不使用下划线。

```cpp
// 全局函数
void ProcessData();
std::string GetFileName();
void CalculateTotalAmount();

// 命名空间内的函数
namespace utils {
void ConvertToUpperCase();
bool IsValidEmailAddress();
}  // namespace utils
```

#### 类成员函数

类成员函数（实例方法）使用**驼峰命名法**（PascalCase）。

```cpp
class FileProcessor {
 public:
  // 公共成员函数
  void OpenFile();
  void CloseFile();
  std::string ReadLine();
  bool WriteData(const std::string& data);

 private:
  // 私有成员函数
  void InternalProcess();
  bool ValidateInput();
};
```

#### 静态成员函数

静态成员函数使用**驼峰命名法**（PascalCase），与普通成员函数保持一致。

```cpp
class StringUtil {
 public:
  // 静态成员函数
  static std::string Trim(const std::string& str);
  static bool StartsWith(const std::string& str, const std::string& prefix);
  static std::vector<std::string> Split(const std::string& str, char delimiter);
};

// 调用方式
std::string trimmed = StringUtil::Trim(input);
bool is_valid = StringUtil::StartsWith(filename, "prefix_");
```

#### 私有成员函数

私有成员函数同样使用**驼峰命名法**（PascalCase）。如果需要在函数名中体现私有性质，可以使用下划线前缀（可选）。

```cpp
class DataManager {
 public:
  void LoadData();
  void SaveData();

 private:
  // 私有成员函数
  void InitializeCache();
  void CleanupResources();

  // 或使用下划线前缀（可选风格）
  void _InternalProcess();
  bool _ValidateConfiguration();
};
```

#### 访问器（Getter）和修改器（Setter）

访问器和修改器与对应的成员变量名保持一致，使用小写加下划线。

```cpp
class Student {
 public:
  // Getter - 与成员变量名一致（去掉尾下划线）
  int student_id() const { return student_id_; }
  std::string full_name() const { return full_name_; }
  int age() const { return age_; }

  // Setter - set_前缀 + 成员变量名（去掉尾下划线）
  void set_student_id(int student_id) { student_id_ = student_id; }
  void set_full_name(const std::string& full_name) { full_name_ = full_name; }
  void set_age(int age) { age_ = age; }

  // 布尔类型的Getter可以使用is_/has_/should_前缀
  bool is_active() const { return is_active_; }
  bool has_permission() const { return has_permission_; }
  bool should_update() const { return should_update_; }

 private:
  int student_id_;
  std::string full_name_;
  int age_;
  bool is_active_;
  bool has_permission_;
  bool should_update_;
};
```

#### 回调函数和函数对象

回调函数和函数对象的命名应清晰表达其用途。

```cpp
// 回调函数类型定义
using DataReceivedCallback = std::function<void(const std::vector<uint8_t>&)>;
using ErrorHandler = std::function<void(int error_code, const std::string& message)>;

// 函数对象（Functor）
class StringComparator {
 public:
  bool operator()(const std::string& a, const std::string& b) const {
    return a.length() < b.length();
  }
};

// Lambda表达式（使用时遵循变量命名规则）
auto process_data = [](const std::string& data) { return data.length(); };
```

#### 虚函数和纯虚函数

虚函数和纯虚函数使用**驼峰命名法**（PascalCase），与普通函数保持一致。

```cpp
class Shape {
 public:
  // 纯虚函数
  virtual double CalculateArea() const = 0;
  virtual double CalculatePerimeter() const = 0;

  // 虚函数
  virtual void Draw() const;
  virtual void PrintInfo() const;

  // 虚析构函数
  virtual ~Shape() = default;
};

class Rectangle : public Shape {
 public:
  // 重写虚函数
  double CalculateArea() const override;
  double CalculatePerimeter() const override;
  void Draw() const override;
};
```

#### 构造函数和析构函数

构造函数和析构函数与类名保持一致（这是语言要求）。

```cpp
class NetworkConnection {
 public:
  // 默认构造函数
  NetworkConnection();

  // 带参数的构造函数
  explicit NetworkConnection(const std::string& host);
  NetworkConnection(const std::string& host, int port);

  // 拷贝构造函数
  NetworkConnection(const NetworkConnection& other);

  // 移动构造函数
  NetworkConnection(NetworkConnection&& other) noexcept;

  // 析构函数
  ~NetworkConnection();
};
```

#### 运算符重载函数

运算符重载函数遵循标准运算符名称。

```cpp
class ComplexNumber {
 public:
  // 算术运算符
  ComplexNumber operator+(const ComplexNumber& other) const;
  ComplexNumber operator-(const ComplexNumber& other) const;
  ComplexNumber operator*(const ComplexNumber& other) const;
  ComplexNumber operator/(const ComplexNumber& other) const;

  // 复合赋值运算符
  ComplexNumber& operator+=(const ComplexNumber& other);
  ComplexNumber& operator-=(const ComplexNumber& other);

  // 比较运算符
  bool operator==(const ComplexNumber& other) const;
  bool operator!=(const ComplexNumber& other) const;
  bool operator<(const ComplexNumber& other) const;

  // 流运算符（友元函数）
  friend std::ostream& operator<<(std::ostream& os, const ComplexNumber& c);
  friend std::istream& operator>>(std::istream& is, ComplexNumber& c);
};
```

#### 模板函数

模板函数的命名与普通函数一致，使用**驼峰命名法**（PascalCase）。

```cpp
// 普通模板函数
template <typename T>
T Max(T a, T b) {
  return (a > b) ? a : b;
}

// 模板特化
std::string Max(const std::string& a, const std::string& b);

// 类模板中的成员函数
template <typename T, typename U>
class Pair {
 public:
  void SetFirst(const T& value);
  void SetSecond(const U& value);
  T GetFirst() const;
  U GetSecond() const;
};
```

#### 函数命名总结表

| 函数类型 | 命名风格 | 示例 |
|---------|---------|------|
| 全局函数 | PascalCase | `ProcessData()` |
| 类成员函数（public） | PascalCase | `OpenFile()` |
| 类成员函数（private） | PascalCase | `InternalProcess()` |
| 静态成员函数 | PascalCase | `Trim()` |
| Getter | 小写+下划线 | `student_id()` |
| Setter | set_前缀+小写 | `set_student_id()` |
| 布尔Getter | is_/has_/should_前缀 | `is_active()` |
| 虚函数 | PascalCase | `CalculateArea()` |
| 纯虚函数 | PascalCase | `Draw() = 0` |
| 回调函数 | PascalCase+Callback后缀 | `OnDataReceivedCallback` |
| 模板函数 | PascalCase | `Max()` |

### 枚举命名

优先使用 `enum class`。类型名按类型命名（PascalCase）。**枚举值按常量命名**（`k` + 混合大小写），不要写成宏风格的全大写，也不要使用 `E_` 前缀。

```cpp
enum class UrlTableError {
  kOk = 0,
  kOutOfMemory,
  kMalformedInput,
};

enum class HttpStatusCode {
  kOk = 200,
  kNotFound = 404,
  kServerError = 500,
};
```

历史代码中的宏风格枚举值（如 `OUT_OF_MEMORY`）仅在与周边一致时保留；**新代码用 `k` 风格**。

### 宏命名

宏命名使用全大写和下划线。

```cpp
#define ROUND(x) ...
#define PI_ROUNDED 3.0
```

---

## 注释

### 注释风格

使用 `//` 或 `/* */`，只要一致即可。文件、类、函数注释使用 `/** */`（Javadoc 风格）。

### 文件注释

每个文件的开头应有版权声明和文件描述。

```cpp
// Copyright (c) 2024 Your Company
//
// Licensed under the Apache License, Version 2.0 ...
//
// File: server/base/logging.cc
// Brief: 实现日志记录功能
```

### 类注释

每个类定义都应附带描述其功能和用法的注释。

```cpp
// Iterates over the contents of a GargantuanTable.
// Example:
//   GargantuanTableIterator* iter = table->NewIterator();
//   for (iter->Seek("foo"); !iter->done(); iter->Next()) {
//     process(iter->key(), iter->value());
//   }
//   delete iter;
class GargantuanTableIterator {
  ...
};
```

### 函数注释

函数声明处的注释描述函数功能；定义处的注释描述实现细节。

```cpp
// Returns an iterator for this table.  It is the client's
// responsibility to delete the iterator when it is done with it,
// and it must not use the iterator once the GargantuanTable object
// on which the iterator was created has been deleted.
//
// The iterator is initially positioned at the beginning of the table.
//
// This method is equivalent to:
//    Iterator* iter = table->NewIterator();
//    iter->Seek("");
//    return iter;
// If you are going to immediately seek to another place in the
// returned iterator, it will be faster to use NewIterator()
// and avoid the extra seek.
Iterator* GetIterator() const;
```

### 变量注释

类数据成员应说明其用途。

```cpp
private:
  // Keeps track of the total number of entries in the table.
  // Used to ensure we do not go over the limit. -1 means
  // that we don't yet know how many entries the table has.
  int num_total_entries_;
```

### 实现注释

在代码中比较巧妙的地方添加注释。

```cpp
// Divide result by two, taking into account that x
// contains the carry from the add.
for (int i = 0; i < result->size(); i++) {
  x = (x << 8) + (*result)[i];
  (*result)[i] = x >> 1;
  x &= 1;
}
```

### TODO 注释

使用 `TODO` 标记待办事项，格式为 `TODO(用户名): 描述`。

```cpp
// TODO(kl@gmail.com): Use a "*" here for concatenation operator.
// TODO(Zeke) change this to use relations.
```

### 弃用注释

使用 `DEPRECATED` 标记废弃的代码。

```cpp
// DEPRECATED: Use NewFunction() instead.
void OldFunction();
```

---

## 格式

### 行长度

每行代码字符数不超过 80。如果注释超过 80 字符，应放在下一行。

### 非 ASCII 字符

尽量不使用非 ASCII 字符，必要时使用 UTF-8 编码。

### 空格 vs 制表符

只使用空格，每次缩进 2 个空格。

### 函数声明与定义

返回类型和函数名在同一行，参数也尽量放在同一行。

```cpp
ReturnType ClassName::ReallyLongFunctionName(Type par_name1,
                                             Type par_name2,
                                             Type par_name3) {
  DoSomething();
  ...
}
```

### Lambda 表达式

```cpp
std::sort(foo.begin(), foo.end(), [](Foo a, Foo b) {
  return a.bar < b.bar;
});
```

### 条件语句

```cpp
if (condition) {
  DoSomething();
} else if (other_condition) {
  DoOtherThing();
} else {
  DoNothing();
}
```

### 循环和 switch 语句

```cpp
switch (var) {
  case 0:
    ...
    break;
  case 1:
    ...
    break;
  default:
    assert(false);
}
```

### 指针和引用表达式

```cpp
char* c;  // 指针
const std::string& str;  // 引用
```

### 布尔表达式

如果布尔表达式超过标准行宽，断行要统一。

```cpp
if (this_one_thing > this_other_thing &&
    a_third_thing == a_fourth_thing &&
    yet_another && last_one) {
  ...
}
```

### 函数返回值

不要在 `return` 表达式里加上非必要的圆括号。

```cpp
return result;  // 不要写成 return (result);
```

### 预处理指令

预处理指令不要缩进，从行首开始。

```cpp
#if defined(ENABLE_FEATURE)
  ...
#endif
```

### 类格式

```cpp
class MyClass : public OtherClass {
 public:
  MyClass();
  explicit MyClass(int var);
  ~MyClass() {}

  void SomeFunction();
  void SomeFunctionThatDoesNothing() {}

  void set_some_var(int var) { some_var_ = var; }
  int some_var() const { return some_var_; }

 private:
  bool SomeInternalFunction();

  int some_var_;
  int some_other_var_;
};
```

### 初始化列表

构造函数初始化列表放在同一行或按四格缩进并排成几行。

```cpp
// 全部放在同一行
MyClass::MyClass(int var) : some_var_(var) {
  DoSomething();
}

// 长列表分行
MyClass::MyClass(int var)
    : some_var_(var),
      some_other_var_(var + 1) {
  DoSomething();
}
```

### 命名空间格式化

命名内容不缩进。

```cpp
namespace {

void foo() {
  ...
}

}  // namespace
```

### 水平留白

```cpp
// 操作符两边加空格
int x = 0;
x = a + b;
x = a | b;

// 分号前不加空格
int a = b;

// 函数调用和函数名间不留空格
bool result = DoSomething(a, b, c);

// 控制语句关键字后加空格
if (condition) {
  ...
}
```

### 垂直留白

减少不必要的空行。

---

## 规则例外

### 现有不合规范的代码

对于现有代码，如果修改时保持与周围代码风格一致。

### Windows 代码

Windows 程序员有自己的习惯，主要变更：

- 文件命名使用驼峰式，如 `MyFile.cpp`
- 不使用异常

---

## 总结

遵循本指南可以使代码保持一致性，提高可读性和可维护性。记住：

1. 保持一致性比遵循规则更重要
2. 在修改现有代码时，遵循周围代码的风格
3. 有疑问时，优先选择可读性
4. 与 Google 原文冲突时，以 [官方 cppguide](https://google.github.io/styleguide/cppguide.html) 为准；本文是中文摘要，并在「函数 `{}`」等处标明了团队例外

---

*本指南基于 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) 编写；前置声明、常量 `k`、枚举值命名已按现行官网校正。*

