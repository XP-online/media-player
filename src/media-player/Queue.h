#pragma once

#include <vector>
#include <mutex> 

template<typename T>
// 线程安全的队列
class Queue
{
public:
	Queue() {
		q.clear();
	}
	// push 入列
	void push(T val) { 
		m.lock();
		q.push_back(val);
		m.unlock();
	}
	// pull 出列，返回false说明队列为空
	bool pull(T &val) { 
		m.lock();
		if (q.empty()) {
			m.unlock();
			return false;
		} else {
			val = q.front();
			q.erase(q.begin());
			m.unlock();
			return true;
		}
	}
	// empty 返回队列是否为空
	bool empty() { 
		m.lock();
		bool isEmpty = q.empty();
		m.unlock();
		return isEmpty;
	}
	// size 返回队列的大小
	int size() { 
		m.lock();
		int s = q.size();
		m.unlock();
		return s;
	}
protected:
	std::vector<T> q; // 容器
	std::mutex     m; // 锁
};
