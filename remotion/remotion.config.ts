/**
 * Note: When using the Node.JS APIs, the config file
 * doesn't apply. Instead, pass options directly to the APIs.
 *
 * All configuration options: https://remotion.dev/docs/config
 */

import { Config } from "@remotion/cli/config";

Config.setRspack(true);
Config.setVideoImageFormat("jpeg");
Config.setOverwriteOutput(true);
Config.setDefaultPremountInSeconds(2);

/**
 * 液面镜头（镜头 08/09）用片元着色器做光线步进，
 * headless 软件 WebGL 下单帧可能超过默认的 30 秒上限，
 * 因此把渲染超时放宽到 4 分钟。
 */
Config.setTimeoutInMilliseconds(240000);
