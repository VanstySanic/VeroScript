// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export struct JsonDefault { public: int count=json::converter<int>::read(json::from_int(19)); };
