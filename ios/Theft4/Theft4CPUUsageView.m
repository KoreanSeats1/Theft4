#import "Theft4CPUUsageView.h"
#import <QuartzCore/QuartzCore.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/thread_info.h>
#include <sys/resource.h>
#include "theft4_metal_presenter.h"

@interface Theft4CPUSnapshot : NSObject
@property BOOL valid;
@property double appCores;
@property NSUInteger activeCores;
@property NSUInteger threadCount;
@property NSUInteger measuredThreads;
@property BOOL threadsValid;
@property double sampleMS;
@property NSArray<NSNumber *> *coreUsage;
@property NSArray<NSDictionary *> *threads;
@property NSString *thermal;
@property NSString *logRows;
@end
@implementation Theft4CPUSnapshot
@end

// Confined to the sampling queue. Each monitoring session starts a new baseline.
@interface Theft4CPUSampler : NSObject
- (Theft4CPUSnapshot *)sample;
@end
@implementation Theft4CPUSampler {
    CFTimeInterval _lastTime;
    double _lastProcessCPU;
    BOOL _lastProcessValid;
    BOOL _coreAccessUnavailable;
    NSArray<NSNumber *> *_lastCoreTicks;
    NSDictionary<NSNumber *, NSNumber *> *_lastThreadCPU;
}
- (Theft4CPUSnapshot *)sample {
    CFTimeInterval begin = CACurrentMediaTime();
    const uint64_t frame = theft4_frame_counter_published_frames();
    const double elapsed = _lastTime ? begin - _lastTime : 0;
    Theft4CPUSnapshot *sample = [Theft4CPUSnapshot new];
    sample.activeCores = NSProcessInfo.processInfo.activeProcessorCount;
    switch (NSProcessInfo.processInfo.thermalState) {
        case NSProcessInfoThermalStateNominal: sample.thermal = @"nominal"; break;
        case NSProcessInfoThermalStateFair: sample.thermal = @"fair"; break;
        case NSProcessInfoThermalStateSerious: sample.thermal = @"serious"; break;
        case NSProcessInfoThermalStateCritical: sample.thermal = @"critical"; break;
    }
    struct rusage usage = {0};
    BOOL processValid = getrusage(RUSAGE_SELF, &usage) == 0;
    if (processValid) {
        double total = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
            (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
        sample.valid = _lastProcessValid && elapsed > 0 && total >= _lastProcessCPU;
        if (sample.valid) sample.appCores = (total - _lastProcessCPU) / elapsed;
        _lastProcessCPU = total;
    }
    _lastProcessValid = processValid;

    // Public Mach API; some iOS versions deny host-level CPU information.
    // Never substitute app thread values for a physical-core measurement.
    if (!_coreAccessUnavailable) {
        natural_t cores = 0;
        processor_info_array_t data = NULL;
        mach_msg_type_number_t count = 0;
        mach_port_t host = mach_host_self();
        kern_return_t result = host_processor_info(host, PROCESSOR_CPU_LOAD_INFO,
                                                   &cores, &data, &count);
        mach_port_deallocate(mach_task_self(), host);
        if (result == KERN_SUCCESS && data && cores > 0 && cores <= 64 &&
            count >= cores * CPU_STATE_MAX) {
            NSMutableArray *ticks = [NSMutableArray arrayWithCapacity:cores * CPU_STATE_MAX];
            NSMutableArray *bars = [NSMutableArray arrayWithCapacity:cores];
            BOOL baseline = _lastCoreTicks.count == cores * CPU_STATE_MAX;
            for (NSUInteger core = 0; core < cores; ++core) {
                uint64_t total = 0, idle = 0;
                for (NSUInteger state = 0; state < CPU_STATE_MAX; ++state) {
                    NSUInteger index = core * CPU_STATE_MAX + state;
                    uint32_t tick = (uint32_t)data[index];
                    [ticks addObject:@(tick)];
                    if (baseline) {
                        // Mach tick counters wrap; unsigned subtraction retains the delta.
                        uint32_t delta = tick - _lastCoreTicks[index].unsignedIntValue;
                        total += delta;
                        if (state == CPU_STATE_IDLE) idle = delta;
                    }
                }
                if (baseline) [bars addObject:@(total ? (double)(total - idle) / total : 0)];
            }
            _lastCoreTicks = ticks;
            sample.coreUsage = bars;
        } else {
            _coreAccessUnavailable = YES;
        }
        if (data) vm_deallocate(mach_task_self(), (vm_address_t)data,
                                (vm_size_t)count * sizeof(integer_t));
    }

    thread_act_array_t threads = NULL;
    mach_msg_type_number_t threadCount = 0;
    NSMutableDictionary *totals = [NSMutableDictionary new];
    NSMutableArray *rows = [NSMutableArray new];
    if (task_threads(mach_task_self(), &threads, &threadCount) == KERN_SUCCESS) {
        sample.threadsValid = YES;
        sample.threadCount = threadCount;
        // Bound diagnostic work even if a faulty run creates excessive threads.
        for (NSUInteger i = 0; i < threadCount; ++i) {
            if (i < 256) {
                thread_identifier_info_data_t identity = {0};
                mach_msg_type_number_t idCount = THREAD_IDENTIFIER_INFO_COUNT;
                thread_extended_info_data_t detail = {0};
                mach_msg_type_number_t detailCount = THREAD_EXTENDED_INFO_COUNT;
                BOOL identified = thread_info(threads[i], THREAD_IDENTIFIER_INFO,
                    (thread_info_t)&identity, &idCount) == KERN_SUCCESS;
                BOOL measured = identified && thread_info(threads[i], THREAD_EXTENDED_INFO,
                    (thread_info_t)&detail, &detailCount) == KERN_SUCCESS;
                if (measured) {
                    ++sample.measuredThreads;
                    double cpu = (detail.pth_user_time + detail.pth_system_time) / 1e9;
                    NSNumber *key = @(identity.thread_id);
                    NSNumber *prior = _lastThreadCPU[key];
                    totals[key] = @(cpu);
                    {
                        detail.pth_name[sizeof(detail.pth_name) - 1] = 0;
                        NSString *name = [NSString stringWithUTF8String:detail.pth_name];
                        if (!name.length) name = [NSString stringWithFormat:@"Thread %llu",
                            (unsigned long long)identity.thread_id];
                        name = [name stringByReplacingOccurrencesOfString:@"Theft4 " withString:@""];
                        double fraction = elapsed > 0 && prior && cpu >= prior.doubleValue
                            ? MIN(1.0, MAX(0.0, (cpu - prior.doubleValue) / elapsed)) : -1;
                        [rows addObject:@{@"name":name, @"usage":@(fraction),
                            @"id":key, @"cpu_seconds":@(cpu), @"run_state":@(detail.pth_run_state),
                            @"priority":@(detail.pth_curpri), @"policy":@(detail.pth_policy)}];
                    }
                }
            }
            // task_threads creates a send right for every returned thread.
            mach_port_deallocate(mach_task_self(), threads[i]);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)threads,
                      (vm_size_t)threadCount * sizeof(thread_t));
    }
    [rows sortUsingComparator:^NSComparisonResult(NSDictionary *left, NSDictionary *right) {
        return [right[@"usage"] compare:left[@"usage"]];
    }];
    sample.threads = rows;
    _lastThreadCPU = totals;
    _lastTime = begin;
    sample.sampleMS = (CACurrentMediaTime() - begin) * 1000.0;
    const unsigned long long timestamp = (unsigned long long)(begin * 1e9);
    NSMutableString *log = [NSMutableString stringWithFormat:
        @"cpu_app,%llu,%llu,,valid=%d core_equivalents=%.4f active_cores=%lu threads=%lu measured_threads=%lu threads_valid=%d thermal=%@ interval_s=%.6f sampler_ms=%.3f\n",
        (unsigned long long)frame, timestamp, sample.valid, sample.appCores,
        (unsigned long)sample.activeCores, (unsigned long)sample.threadCount,
        (unsigned long)sample.measuredThreads, sample.threadsValid, sample.thermal, elapsed, sample.sampleMS];
    if (!sample.coreUsage.count) [log appendFormat:
        @"cpu_core,%llu,%llu,,unavailable_or_baseline_pending=1 scope=device\n",
        (unsigned long long)frame, timestamp];
    for (NSUInteger i = 0; i < sample.coreUsage.count; ++i) [log appendFormat:
        @"cpu_core,%llu,%llu,,index=%lu busy_pct=%.3f scope=device\n",
        (unsigned long long)frame, timestamp, (unsigned long)i,
        sample.coreUsage[i].doubleValue * 100];
    for (NSDictionary *row in rows) {
        NSString *name = [row[@"name"] stringByReplacingOccurrencesOfString:@"\"" withString:@"\"\""];
        name = [[name stringByReplacingOccurrencesOfString:@"\n" withString:@" "]
            stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
        [log appendFormat:@"cpu_thread,%llu,%llu,,\"id=%@ busy_pct=%.3f cpu_seconds=%.6f priority=%@ run_state=%@ policy=%@ name=%@\"\n",
            (unsigned long long)frame, timestamp, row[@"id"],
            [row[@"usage"] doubleValue] < 0 ? -1 : [row[@"usage"] doubleValue] * 100,
            [row[@"cpu_seconds"] doubleValue], row[@"priority"], row[@"run_state"], row[@"policy"], name];
    }
    sample.logRows = log;
    return sample;
}
@end

@implementation Theft4CPUUsageView {
    dispatch_source_t _timer;
    NSUInteger _generation;
    Theft4CPUSnapshot *_snapshot;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if (!(self = [super initWithFrame:frame])) return nil;
    self.backgroundColor = [UIColor colorWithWhite:0 alpha:0.72];
    self.opaque = NO;
    self.layer.cornerRadius = 8;
    self.clipsToBounds = YES;
    self.userInteractionEnabled = NO;
    self.isAccessibilityElement = YES;
    self.accessibilityIdentifier = @"game.cpuUsage";
    self.accessibilityLabel = @"CPU and thread activity";
    self.hidden = YES;
    return self;
}
- (void)setMonitoringActive:(BOOL)active {
    NSAssert(NSThread.isMainThread, @"CPU HUD lifecycle belongs to main");
    self.hidden = !active;
    if (active == (_timer != nil)) return;
    const NSUInteger generation = ++_generation;
    if (!active) {
        dispatch_source_cancel(_timer);
        _timer = nil;
        _snapshot = nil;
        self.accessibilityValue = nil;
        return;
    }
    Theft4CPUSampler *sampler = [Theft4CPUSampler new];
    dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(
        DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0);
    dispatch_queue_t queue = dispatch_queue_create("Theft4 CPU sampler", attr);
    // At most one delivery can be outstanding, even while main is stalled.
    dispatch_semaphore_t deliveryBudget = dispatch_semaphore_create(1);
    _timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_source_set_timer(_timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                              NSEC_PER_SEC, NSEC_PER_SEC / 10);
    __weak Theft4CPUUsageView *weakSelf = self;
    dispatch_source_set_event_handler(_timer, ^{
        if (dispatch_semaphore_wait(deliveryBudget, DISPATCH_TIME_NOW) != 0) return;
        @autoreleasepool {
            Theft4CPUSnapshot *snapshot = [sampler sample];
            dispatch_async(dispatch_get_main_queue(), ^{
                Theft4CPUUsageView *view = weakSelf;
                if (view && view->_generation == generation && view->_timer) {
                    view->_snapshot = snapshot;
                    [view setNeedsDisplay];
                    if (view.sampleLogHandler) view.sampleLogHandler(snapshot.logRows);
                }
                dispatch_semaphore_signal(deliveryBudget);
            });
        }
    });
    dispatch_resume(_timer);
    [self setNeedsDisplay];
}
- (void)drawRect:(CGRect)rect {
    Theft4CPUSnapshot *s = _snapshot;
    NSDictionary *text = @{NSFontAttributeName:[UIFont monospacedDigitSystemFontOfSize:10
        weight:UIFontWeightMedium], NSForegroundColorAttributeName:UIColor.whiteColor};
    NSMutableParagraphStyle *truncate = [NSMutableParagraphStyle new];
    truncate.lineBreakMode = NSLineBreakByTruncatingTail;
    NSDictionary *small = @{NSFontAttributeName:[UIFont monospacedDigitSystemFontOfSize:9
        weight:UIFontWeightRegular], NSForegroundColorAttributeName:[UIColor colorWithWhite:1 alpha:0.8],
        NSParagraphStyleAttributeName:truncate};
    const CGFloat width = self.bounds.size.width - 16;
    NSString *title = s.valid ? [NSString stringWithFormat:@"App CPU: %.1f cores", s.appCores]
                             : @"App CPU: sampling…";
    [title drawAtPoint:CGPointMake(8, 6) withAttributes:text];
    [@"Device cores · all apps · % busy" drawAtPoint:CGPointMake(8, 21) withAttributes:small];
    if (s.coreUsage.count) {
        CGFloat step = width / s.coreUsage.count;
        for (NSUInteger i = 0; i < s.coreUsage.count; ++i) {
            const double fraction = MIN(1, MAX(0, s.coreUsage[i].doubleValue));
            [[NSString stringWithFormat:@"%.0f", fraction * 100]
                drawInRect:CGRectMake(8+i*step, 34, step, 11) withAttributes:small];
            CGRect bar = CGRectMake(8+i*step, 47, MAX(1, step-3), 22);
            [[UIColor colorWithWhite:1 alpha:0.14] setFill]; UIRectFill(bar);
            bar.origin.y += bar.size.height * (1-fraction); bar.size.height *= fraction;
            [[UIColor colorWithRed:0.35 green:0.93 blue:0.77 alpha:1] setFill]; UIRectFill(bar);
            [[NSString stringWithFormat:@"%lu", (unsigned long)i]
                drawAtPoint:CGPointMake(8+i*step, 71) withAttributes:small];
        }
    } else {
        [@"Core data unavailable / warming up" drawInRect:CGRectMake(8, 48, width, 13) withAttributes:small];
    }
    [@"App threads · 100% = one core" drawAtPoint:CGPointMake(8, 87) withAttributes:small];
    for (NSUInteger i = 0; i < MIN(s.threads.count, 6); ++i) {
        NSDictionary *row = s.threads[i];
        const double fraction = [row[@"usage"] doubleValue];
        const CGFloat y = 102+i*12;
        [[UIColor colorWithRed:0.35 green:0.93 blue:0.77 alpha:0.22] setFill];
        UIRectFill(CGRectMake(8, y, width*MAX(0, fraction), 11));
        [row[@"name"] drawInRect:CGRectMake(9, y, width-38, 12) withAttributes:small];
        [(fraction < 0 ? @"—" : [NSString stringWithFormat:@"%.0f%%", fraction*100])
            drawAtPoint:CGPointMake(self.bounds.size.width-38, y) withAttributes:small];
    }
    if (!s.threads.count)
        [@"Thread data unavailable / warming up" drawInRect:CGRectMake(8, 104, width, 13) withAttributes:small];
    [[NSString stringWithFormat:@"1 Hz · sample %.2f ms · %lu/%lu threads",
        s.sampleMS, (unsigned long)s.measuredThreads, (unsigned long)s.threadCount]
        drawInRect:CGRectMake(8, 177, width, 12) withAttributes:small];
    self.accessibilityValue = [NSString stringWithFormat:@"%@. Device cores include all apps. Busiest app threads: %@",
        title, s.threads.description ?: @"Waiting for sample"];
}
- (void)dealloc {
    if (_timer) dispatch_source_cancel(_timer);
}
@end
