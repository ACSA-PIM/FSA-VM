# FSA-VM: Fast, Scalable, and Accurate Virtual Memory Simulation Framework

## Overview
FSA-VM aims to provide a comprehensive framework for simulating and evaluating virtual memory systems. This framework integrates various virtual memory management designs and optimizations at multiple levels, including hardware components like page tables, TLBs, MMUs, and software layers. It leverages existing simulators like zsim and ramulator for high scalability and accuracy, facilitating effective performance evaluation of virtual memory management. This repository is based on MultiPIM.(https://github.com/Systems-ShiftLab/MultiPIM)

## Features
- **Page Table Walking Cache (PWC)**: Effectively simulates the hardware performance of page table walking caches in multicore systems.
- **Multi-Level TLB**: Supports simulations of multi-level TLB architectures to evaluate their impact on performance.
- **Multiple Address Translation Mechanisms**: Incorporates various address translation mechanisms to explore different memory management techniques.
- **Benchmarks**: Uses a variety of applications with different memory access patterns for analysis, focusing on normal data and metadata, to assess performance.

## Evaluation
1. **Validation**: Compares key performance indicators such as TLB, PTW with real machine metrics to demonstrate simulation accuracy.
2. **Simulation Speed**: Benchmarks against other virtual memory simulators like Victima to highlight the speed advantages of FSA-VM in both single-core and large-scale core scenarios.
3. **Hardware and Software Co-design Simulation**: Analyzes the combination of hardware and software designs in virtual memory management, providing key experimental insights and results.
4. **Scalability Analysis**: Assesses the scalability of the simulation framework in large-scale core and PIM architectures, focusing on the overhead of address translation.
5. **Application Characteristic Simulation**: Identifies and analyzes different memory access patterns in virtual memory management.

## Usage
Instructions on how to set up and run simulations with FSA-VM will be provided here.

## Contributing
Guidelines for contributing to the FSA-VM project will be outlined here.

## License
Details about the project's licensing will be included here.

---

# FSA-VM：快速、可扩展和精确的虚拟内存模拟框架

## 概述
FSA-VM 旨在提供一个全面的虚拟内存系统模拟和评估框架。该框架整合了包括页表、TLB、MMU在内的硬件组件和软件层面的多种虚拟内存管理设计与优化。它利用了zsim和ramulator等现有模拟器，以实现高可扩展性和准确性，有效评估虚拟内存管理的性能。

## 特性
- **页表走行缓存(PWC)**：有效模拟多核系统下的页表走行缓存硬件性能模拟。
- **多级TLB**: 支持多级TLB架构的模拟，评估其对性能的影响。
- **多种地址翻译机制**: 集成多种地址翻译机制，探索不同的内存管理技术。
- **基准测试**: 使用多种不同内存访问模式的应用进行分析，重点关注普通数据和元数据，以评估性能。

## 评估
1. **验证**: 与实机的TLB、PTW等关键部件的性能指标进行对比，展示模拟的精确度。
2. **模拟速度**: 与Victima等其他虚拟内存模拟器进行基准测试，突出在单核和大规模核场景下FSA-VM的速度优势。
3. **软硬件协同设计模拟**: 分析虚拟内存管理中硬件和软件设计的结合，提供关键的实验洞察和结果。
4. **可扩展性分析**: 评估模拟框架在大规模核心和PIM架构中的可扩展性，专注于地址翻译的开销。
5. **应用特征模拟**: 识别并分析虚拟内存管理中应用的不同内存访问模式。

## 使用说明
此处将提供如何设置和运行FSA-VM模拟的说明。

## 贡献指南
此处将概述向FSA-VM项目贡献的指南。

## 许可证
此处将包括项目许可证的详细信息。
