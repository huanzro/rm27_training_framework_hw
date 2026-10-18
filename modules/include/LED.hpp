//
// LED.hpp —— C 板板载 RGB LED。新建文件。
//

#ifndef LED_HPP
#define LED_HPP

namespace LED {

//new: 三色灯控制。参数为 true 表示点亮该颜色
void Set(bool red, bool green, bool blue);

//new: 全灭
void Off();

}  // namespace LED

#endif // LED_HPP
