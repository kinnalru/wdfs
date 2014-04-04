#pragma once

#include <iostream>
#include <string>

struct detail {
    static int tab;
};

template <typename Exit = std::string>
struct scoped_logger {
    scoped_logger(const Exit& ex) : do_exit(true), exit(ex) {}
    scoped_logger() : do_exit(false) {}

    ~scoped_logger() {
        --detail::tab;        
        if (!do_exit) return;

        std::cerr << get_tab() << "<< " << pretty.c_str() << ": " << exit << std::endl;
    }

    template <typename Enter>
    void log_enter(const Enter& enter) {
        std::cerr << get_tab() << ">> " << pretty.c_str() << ": " << enter << std::endl;
        ++detail::tab;
    }
    void log_enter() {log_enter("");}

    void set_pretty(const std::string& p ) {pretty = p;}
    
    static std::string get_tab() {
        return std::string(detail::tab * 4, ' ');
    };
    
private:
    std::string pretty;
    bool do_exit;
    Exit exit;

};


#define LOG_ENTER(str) scoped_logger<int> __log_e; __log_e.set_pretty(__func__); __log_e.log_enter(str);
//#define LOG_EXIT(str) scoped_logger<decltype(str)> __log_x(str); __log_x.set_pretty(__func__);
#define LOG_ENEX(str1, str2) scoped_logger<decltype(str2)> __log(str2); __log.set_pretty(__func__); __log.log_enter(str1);

/*макрос для печати отладочной информации. Если приживется...*/
#define wdfs_dbg(format, arg...) do { \
        if (wdfs_cfg.debug == true) \
            fprintf(stderr, "%s++ " format, scoped_logger<int>::get_tab().c_str(), ## arg);\
    } while (0)
    
#define wdfs_err(format, arg...) do { \
        if (wdfs_cfg.debug == true) \
            fprintf(stderr,"%s!! " format, scoped_logger<int>::get_tab().c_str(), ## arg);\
    } while (0)    
    
#define wdfs_pr(format, arg...) do { \
        if (wdfs_cfg.debug == true) \
            fprintf(stderr, "%s" format, scoped_logger<int>::get_tab().c_str(), ## arg);\
    } while (0)    

